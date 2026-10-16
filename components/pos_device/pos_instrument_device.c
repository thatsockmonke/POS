#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pos_device.h"
#include "pos_peripherals.h"
#include <stdio.h>
static uint8_t imu_address;
static bool motion_ready;
static int read_reg(void *ctx, uint8_t reg, uint8_t *data, size_t n) {
  (void)ctx;
  return pos_device_i2c_read_block(imu_address, reg, data, n);
}
static int write_reg(void *ctx, uint8_t reg, const uint8_t *data, size_t n) {
  (void)ctx;
  return pos_device_i2c_write_block(imu_address, reg, data, n);
}
static void delay(void *ctx, unsigned ms) {
  (void)ctx;
  vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}
static const pos_imu_bus_t bus = {
    .read = read_reg, .write = write_reg, .delay_ms = delay};
int pos_motion_start(pos_error_t *e) {
  if (motion_ready)
    return 0;
  uint8_t id = 0;
  imu_address = 0x69;
  if (read_reg(NULL, 0, &id, 1) || id != 0x24)
    imu_address = 0x68;
  int result = pos_imu_initialize(&bus, e);
  motion_ready = !result;
  return result;
}
int pos_motion_read(pos_imu_sample_t *s, pos_error_t *e) {
  if (!motion_ready) {
    if (e)
      snprintf(e->message, sizeof(e->message), "IMU is not initialized.");
    return -1;
  }
  return pos_imu_sample(&bus, s, e);
}
void pos_motion_stop(void) {
  if (motion_ready)
    (void)pos_imu_suspend(&bus);
  motion_ready = false;
}
int pos_infrared_send(uint8_t address, uint8_t command, pos_error_t *e) {
  rmt_channel_handle_t channel = NULL;
  rmt_encoder_handle_t encoder = NULL;
  rmt_tx_channel_config_t config = {.gpio_num = 44,
                                    .clk_src = RMT_CLK_SRC_DEFAULT,
                                    .resolution_hz = 1000000,
                                    .mem_block_symbols = 64,
                                    .trans_queue_depth = 1};
  int result = -1;
  bool enabled = false;
  if (rmt_new_tx_channel(&config, &channel) != ESP_OK)
    goto done;
  rmt_carrier_config_t carrier = {.frequency_hz = 38000, .duty_cycle = 0.33f};
  rmt_copy_encoder_config_t copy = {0};
  if (rmt_apply_carrier(channel, &carrier) != ESP_OK ||
      rmt_new_copy_encoder(&copy, &encoder) != ESP_OK ||
      rmt_enable(channel) != ESP_OK)
    goto done;
  enabled = true;
  uint16_t pairs[34][2];
  rmt_symbol_word_t symbols[34];
  pos_ir_nec(address, command, pairs);
  for (unsigned i = 0; i < 34; ++i)
    symbols[i] = (rmt_symbol_word_t){.level0 = 1,
                                     .duration0 = pairs[i][0],
                                     .level1 = 0,
                                     .duration1 = pairs[i][1]};
  rmt_transmit_config_t tx = {.loop_count = 0};
  if (rmt_transmit(channel, encoder, symbols, sizeof(symbols), &tx) == ESP_OK &&
      rmt_tx_wait_all_done(channel, 500) == ESP_OK)
    result = 0;
done:
  if (enabled)
    rmt_disable(channel);
  if (encoder)
    rmt_del_encoder(encoder);
  if (channel)
    rmt_del_channel(channel);
  if (result && e)
    snprintf(e->message, sizeof(e->message),
             "IR transmit failed; GPIO44 released.");
  return result;
}
