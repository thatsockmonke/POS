#pragma once
#include "pos_core.h"
#include <stdint.h>
/* Internal I2C shared with keyboard, audio codec and cap IO expander. */
int pos_device_i2c_read(uint8_t address,uint8_t reg,uint8_t *data);
int pos_device_i2c_write(uint8_t address,uint8_t reg,uint8_t data);
typedef struct {
  bool display_ok, keyboard_ok, wifi_connected;
  uint32_t keys, frame_us, boot_ms, firmware_bytes;
  uint32_t raw_keys[2], fifo_overflows, queue_drops;
  uint32_t frames, frame_min_us, frame_max_us;
  uint64_t frame_total_us;
} pos_device_info_t;
int pos_device_init(void);
void pos_device_boot_flash(void);
bool pos_device_event(pos_event_t *event, uint32_t timeout_ms);
void pos_device_show(const pos_view_t *view);
void pos_device_brightness(unsigned percent);
int pos_device_battery_mv(
    void); /* calibrated millivolts, 0 means unavailable */
pos_device_info_t pos_device_info(void);
bool pos_device_safe_boot(void);
void pos_device_boot_ready(uint32_t firmware_bytes);
void pos_device_sleep(void);
void pos_device_set_sleep_hook(void (*hook)(void));
/* A session-only network: password never written to SD/NVS or logs. */
int pos_device_wifi(const char *ssid, const char *password,
                    uint32_t timeout_ms);
int pos_device_wifi_prepare(void);
void pos_device_wifi_stop(void);

void pos_device_keyboard_begin(void);

int pos_device_set_time_utc(const char *text);

int pos_device_i2c_read_block(uint8_t,uint8_t,uint8_t *,size_t);
int pos_device_i2c_write_block(uint8_t,uint8_t,const uint8_t *,size_t);
