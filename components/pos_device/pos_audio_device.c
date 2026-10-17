#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "pos_device.h"
#include "pos_peripherals.h"
#include <stdatomic.h>
#include <stdio.h>
static i2s_chan_handle_t output;
static unsigned rate;
static i2s_chan_handle_t input;
/* One second of mono PCM, independent of SD writes and display rendering.
 * Only the capture task reads I2S; only the foreground recorder drains PCM. */
#define MIC_BUFFER_BYTES (16000u * sizeof(int16_t))
static StreamBufferHandle_t mic_buffer;
static SemaphoreHandle_t mic_exited;
static bool mic_task_running, input_enabled;
static atomic_uint mic_left_peak, mic_right_peak;
static atomic_bool mic_overflow, mic_stopping;
enum { MIC_OK, MIC_READ_FAILED, MIC_DMA_OVERRUN, MIC_BUFFER_OVERRUN };
static atomic_int mic_error;
static bool IRAM_ATTR overflow(i2s_chan_handle_t handle,
                               i2s_event_data_t *event, void *ctx) {
  (void)handle;
  (void)event;
  (void)ctx;
  atomic_store_explicit(&mic_overflow, true, memory_order_relaxed);
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

/* M5Unified's default mono input selects RIGHT and applies magnification 16
 * with f_gain = magnification / (over_sampling * 2): 8x at one sample/frame.
 * Keep the stock analog profile; saturate PCM instead of wrapping loud input.
 */
#define MIC_PCM_GAIN 8
static void peak_update(atomic_uint *peak, unsigned value) {
  unsigned previous = atomic_load_explicit(peak, memory_order_relaxed);
  while (previous < value && !atomic_compare_exchange_weak_explicit(
                                 peak, &previous, value, memory_order_relaxed,
                                 memory_order_relaxed))
    ;
}
void pos_microphone_levels(unsigned *left, unsigned *right) {
  *left = atomic_exchange_explicit(&mic_left_peak, 0, memory_order_relaxed);
  *right = atomic_exchange_explicit(&mic_right_peak, 0, memory_order_relaxed);
}
static int capture_read(int16_t mono[256], size_t capacity, size_t *frames) {
  int16_t stereo[512];
  size_t bytes = 0;
  *frames = 0;
  if (atomic_load_explicit(&mic_overflow, memory_order_relaxed))
    return MIC_DMA_OVERRUN;
  if (i2s_channel_read(input, stereo, capacity * 4, &bytes, 100) != ESP_OK ||
      !bytes || bytes % 4)
    return MIC_READ_FAILED;
  if (atomic_load_explicit(&mic_overflow, memory_order_relaxed))
    return MIC_DMA_OVERRUN;
  *frames = bytes / 4;
  unsigned left_peak = 0, right_peak = 0;
  for (size_t i = 0; i < *frames; ++i) {
    int left = stereo[i * 2], right = stereo[i * 2 + 1];
    unsigned l = left < 0 ? -left : left;
    unsigned r = right < 0 ? -right : right;
    if (l > left_peak)
      left_peak = l;
    if (r > right_peak)
      right_peak = r;
    int amplified = right * MIC_PCM_GAIN;
    mono[i] = amplified > INT16_MAX   ? INT16_MAX
              : amplified < INT16_MIN ? INT16_MIN
                                      : amplified;
  }
  peak_update(&mic_left_peak, left_peak);
  peak_update(&mic_right_peak, right_peak);
  return MIC_OK;
}
static void microphone_task(void *unused) {
  (void)unused;
  while (!atomic_load_explicit(&mic_stopping, memory_order_relaxed)) {
    int16_t mono[256];
    size_t frames;
    int error = capture_read(mono, 256, &frames);
    if (!error &&
        xStreamBufferSend(mic_buffer, mono, frames * 2, 0) != frames * 2)
      error = MIC_BUFFER_OVERRUN;
    if (error) {
      atomic_store_explicit(&mic_error, error, memory_order_relaxed);
      break;
    }
  }
  xSemaphoreGive(mic_exited);
  vTaskDelete(NULL);
}
void pos_microphone_end(void) {
  if (mic_task_running) {
    atomic_store_explicit(&mic_stopping, true, memory_order_relaxed);
    /* Capture reads have a 100 ms timeout. Join before freeing I2S or PCM. */
    xSemaphoreTake(mic_exited, portMAX_DELAY);
    mic_task_running = false;
  }
  if (input_enabled) {
    i2s_channel_disable(input);
    input_enabled = false;
  }
}
void pos_microphone_stop(void) {
  pos_microphone_end();
  if (input) {
    pos_device_i2c_write(0x18, 0x0d, 0xfc);
    pos_device_i2c_write(0x18, 0x0e, 0x6a);
    pos_device_i2c_write(0x18, 0x00, 0);
    i2s_del_channel(input);
    input = NULL;
  }
  if (mic_buffer) {
    vStreamBufferDelete(mic_buffer);
    mic_buffer = NULL;
  }
  if (mic_exited) {
    vSemaphoreDelete(mic_exited);
    mic_exited = NULL;
  }
}
int pos_microphone_read(int16_t *mono, size_t capacity, size_t *frames,
                        pos_error_t *e) {
  if (!frames)
    return fail(e, "Invalid microphone buffer.");
  *frames = 0;
  if (!input || !mic_buffer || !mono || !capacity || capacity > 256)
    return fail(e, "Microphone is not recording.");
  int error = atomic_load_explicit(&mic_error, memory_order_relaxed);
  if (error)
    return fail(e,
                error == MIC_BUFFER_OVERRUN
                    ? "SD could not keep up with buffered audio. Partial WAV "
                      "retained; recover it in Recorder."
                    : "Microphone capture interrupted. Partial WAV retained; "
                      "recover it in Recorder.");
  size_t bytes =
      xStreamBufferReceive(mic_buffer, mono, capacity * 2,
                           mic_task_running ? pdMS_TO_TICKS(100) : 0);
  if (!bytes && !mic_task_running)
    return 0; /* End-of-capture: all queued PCM has been delivered. */
  if (!bytes || bytes % 2)
    return fail(e, "Microphone read timed out. Partial WAV retained.");
  *frames = bytes / 2;
  return 0;
}
int pos_microphone_start(pos_error_t *e) {
  if (input || output)
    return fail(e, "Audio is busy. Close playback or recording first.");
  mic_buffer = xStreamBufferCreate(MIC_BUFFER_BYTES, sizeof(int16_t));
  mic_exited = xSemaphoreCreateBinary();
  if (!mic_buffer || !mic_exited) {
    pos_microphone_stop();
    return fail(
        e,
        "Not enough memory for buffered recording. Close services and retry.");
  }
  atomic_store_explicit(&mic_stopping, false, memory_order_relaxed);
  atomic_store_explicit(&mic_error, MIC_OK, memory_order_relaxed);
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  channel.dma_desc_num = 6;
  channel.dma_frame_num = 256;
  if (i2s_new_channel(&channel, NULL, &input) != ESP_OK)
    goto failed;
  i2s_std_config_t config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = I2S_GPIO_UNUSED,
                   .bclk = 41,
                   .ws = 43,
                   .dout = I2S_GPIO_UNUSED,
                   .din = 46}};
  atomic_store_explicit(&mic_overflow, false, memory_order_relaxed);
  i2s_event_callbacks_t callbacks = {.on_recv_q_ovf = overflow};
  if (i2s_channel_init_std_mode(input, &config) != ESP_OK ||
      i2s_channel_register_event_callback(input, &callbacks, NULL) != ESP_OK ||
      i2s_channel_enable(input) != ESP_OK)
    goto failed;
  input_enabled = true;
  /* Pinned M5Unified ADV capture profile. Clock is running before analog arm.
   */
  static const uint8_t regs[][2] = {{0x00, 0x80}, {0x01, 0xba}, {0x02, 0x18},
                                    {0x0d, 0x01}, {0x0e, 0x02}, {0x14, 0x10},
                                    {0x17, 0xbf}, {0x1c, 0x6a}, {0x32, 0x00}};
  for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i)
    if (pos_device_i2c_write(0x18, regs[i][0], regs[i][1]))
      goto failed;
  /* Check stable clock/ADC registers; an I2C ACK alone does not prove setup.
   * RESET/CSM is intentionally excluded because its command bits self-clear. */
  static const uint8_t verify[] = {0x01, 0x02, 0x0e, 0x14, 0x17, 0x1c};
  for (unsigned i = 0; i < sizeof(verify); ++i) {
    uint8_t actual = 0, expected = 0;
    for (unsigned j = 0; j < sizeof(regs) / sizeof(regs[0]); ++j)
      if (regs[j][0] == verify[i])
        expected = regs[j][1];
    if (pos_device_i2c_read(0x18, verify[i], &actual) || actual != expected) {
      pos_microphone_stop();
      return fail(e, "Microphone codec configuration readback failed.");
    }
  }
  /* Discard one second of analog warm-up without accumulating audio in RAM. */
  for (unsigned i = 0; i < 63; ++i) {
    int16_t mono[256];
    size_t n;
    if (capture_read(mono, 256, &n) || !n)
      goto failed;
  }
  atomic_store_explicit(&mic_left_peak, 0, memory_order_relaxed);
  atomic_store_explicit(&mic_right_peak, 0, memory_order_relaxed);
  if (xTaskCreate(microphone_task, "pos_mic", 4096, NULL, 6, NULL) != pdPASS)
    goto failed;
  mic_task_running = true;
  return 0;
failed:
  pos_microphone_stop();
  return fail(e, "Microphone/ES8311 initialization failed.");
}
