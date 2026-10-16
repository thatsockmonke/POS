#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pos_device.h"
#include "pos_peripherals.h"
#include <stdio.h>
static i2s_chan_handle_t output;
static unsigned rate;
static i2s_chan_handle_t input;
static volatile bool mic_overflow;
static bool IRAM_ATTR overflow(i2s_chan_handle_t handle,
                               i2s_event_data_t *event, void *ctx) {
  (void)handle;
  (void)event;
  (void)ctx;
  mic_overflow = true;
  return false;
}
static int fail(pos_error_t *e, const char *text) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return -1;
}
void pos_audio_stop(void) {
  if (output) {
    pos_device_i2c_write(0x18, 0x32, 0);
    i2s_channel_disable(output);
    i2s_del_channel(output);
    output = NULL;
  }
  rate = 0;
}
int pos_audio_write(const int16_t *stereo, size_t frames, unsigned hz,
                    pos_error_t *e) {
  if (input)
    return fail(e, "Stop recording before playing audio.");
  if (!frames)
    return 0;
  if (hz < 8000 || hz > 48000)
    return fail(e, "Unsupported audio sample rate.");
  if (output && rate != hz)
    pos_audio_stop();
  if (!output) {
    i2s_chan_config_t channel =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = 4;
    channel.dma_frame_num = 256;
    if (i2s_new_channel(&channel, &output, NULL) != ESP_OK)
      return fail(e, "Cannot allocate I2S audio channel.");
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                     .bclk = 41,
                     .ws = 43,
                     .dout = 42,
                     .din = I2S_GPIO_UNUSED}};
    if (i2s_channel_init_std_mode(output, &config) != ESP_OK ||
        i2s_channel_enable(output) != ESP_OK) {
      pos_audio_stop();
      return fail(e, "Audio channel setup failed.");
    }
    /* M5Unified Cardputer ADV ES8311 profile: BCLK supplies the codec clock. */
    const uint8_t registers[][2] = {{0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18},
                                    {0x0D, 0x01}, {0x12, 0x00}, {0x13, 0x10},
                                    {0x32, 0xBF}, {0x37, 0x08}};
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i)
      if (pos_device_i2c_write(0x18, registers[i][0], registers[i][1])) {
        pos_audio_stop();
        return fail(e, "ES8311 codec not detected. Audio stopped.");
      }
    rate = hz;
  }
  size_t written = 0;
  return i2s_channel_write(output, stereo, frames * 4, &written, 500) ==
                     ESP_OK &&
                 written == frames * 4
             ? 0
             : fail(e, "Audio output interrupted.");
}

void pos_microphone_stop(void) {
  if (!input)
    return;
  pos_device_i2c_write(0x18, 0x0d, 0xfc);
  pos_device_i2c_write(0x18, 0x0e, 0x6a);
  pos_device_i2c_write(0x18, 0x00, 0);
  i2s_channel_disable(input);
  i2s_del_channel(input);
  input = NULL;
}
int pos_microphone_read(int16_t *mono, size_t capacity, size_t *frames,
                        pos_error_t *e) {
  int16_t stereo[512];
  size_t bytes = 0;
  *frames = 0;
  if (mic_overflow)
    return fail(e, "Microphone overrun during SD/UI work. Recording stopped; "
                   "partial WAV retained.");
  if (!input || !mono || !capacity || capacity > 256)
    return fail(e, "Microphone is not recording.");
  if (i2s_channel_read(input, stereo, capacity * 4, &bytes, 100) != ESP_OK ||
      bytes % 4)
    return fail(e, "Microphone read interrupted. Partial WAV retained.");
  *frames = bytes / 4;
  for (size_t i = 0; i < *frames; ++i)
    mono[i] = stereo[i * 2];
  return 0;
}
int pos_microphone_start(pos_error_t *e) {
  if (input || output)
    return fail(e, "Audio is busy. Close playback or recording first.");
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  channel.dma_desc_num = 6;
  channel.dma_frame_num = 256;
  if (i2s_new_channel(&channel, NULL, &input) != ESP_OK)
    return fail(e, "Cannot allocate microphone I2S.");
  i2s_std_config_t config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                   .bclk = 41,
                   .ws = 43,
                   .dout = I2S_GPIO_UNUSED,
                   .din = 46}};
  mic_overflow = false;
  i2s_event_callbacks_t callbacks = {.on_recv_q_ovf = overflow};
  if (i2s_channel_init_std_mode(input, &config) != ESP_OK ||
      i2s_channel_register_event_callback(input, &callbacks, NULL) != ESP_OK ||
      i2s_channel_enable(input) != ESP_OK)
    goto failed;
  /* Pinned M5Unified ADV capture profile. Clock is running before analog arm.
   */
  static const uint8_t regs[][2] = {{0x00, 0x80}, {0x01, 0xba}, {0x02, 0x18},
                                    {0x0d, 0x01}, {0x0e, 0x02}, {0x14, 0x10},
                                    {0x17, 0xbf}, {0x1c, 0x6a}, {0x32, 0x00}};
  for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i)
    if (pos_device_i2c_write(0x18, regs[i][0], regs[i][1]))
      goto failed;
  /* Discard one second of analog warm-up without accumulating audio in RAM. */
  for (unsigned i = 0; i < 63; ++i) {
    int16_t mono[256];
    size_t n;
    if (pos_microphone_read(mono, 256, &n, e) || !n)
      goto failed;
  }
  return 0;
failed:
  pos_microphone_stop();
  return fail(e, "Microphone/ES8311 initialization failed.");
}
