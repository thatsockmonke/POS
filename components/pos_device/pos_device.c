#include "pos_device.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pos_console.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pos_device_info_t info;
static atomic_uint key_count, raw_keys[2], fifo_overflows, queue_drops;
static QueueHandle_t events;
static SemaphoreHandle_t drawn;
static esp_lcd_panel_handle_t panel;
static i2c_master_dev_handle_t keyboard;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;
static uint16_t *stripe;
static pos_view_t previous;
static bool previous_valid, backlight, wifi_initialized, wifi_started;
static bool serial_input;
static RTC_NOINIT_ATTR uint32_t boot_magic, boot_attempts, force_recovery;
#define BOOT_MAGIC 0x504f5332u
#define RECOVERY_MAGIC 0x52454332u

static bool color_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *ctx) {
  (void)io;
  (void)event;
  (void)ctx;
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR(drawn, &wake);
  return wake == pdTRUE;
}
static int reg_write(uint8_t reg, uint8_t value) {
  uint8_t data[] = {reg, value};
  return i2c_master_transmit(keyboard, data, 2, 25) == ESP_OK ? 0 : -1;
}
static int reg_read(uint8_t reg, uint8_t *value) {
  return i2c_master_transmit_receive(keyboard, &reg, 1, value, 1, 25) == ESP_OK
             ? 0
             : -1;
}
static void enqueue(pos_event_t event) {
  if (event.key == POS_KEY_HOME) {
    xQueueReset(events);
    xQueueSendToFront(events, &event, 0);
  } else if (xQueueSend(events, &event, 0) != pdTRUE)
    atomic_fetch_add_explicit(&queue_drops, 1, memory_order_relaxed);
}
static void input_task(void *unused) {
  (void)unused;
  pos_keyboard_t keys = {0};
  int64_t held_since = 0;
  bool button_down = false;
  for (;;) {
    if (info.keyboard_ok) {
      uint8_t status = 0;
      if (!reg_read(2, &status)) {
        if (status & 8) {
          atomic_fetch_add_explicit(&fifo_overflows, 1, memory_order_relaxed);
          memset(&keys, 0, sizeof(keys));
          for (int i = 0; i < 16; ++i) {
            uint8_t raw;
            if (reg_read(4, &raw) || !raw)
              break;
          }
        }
        for (int i = 0; i < 16; ++i) {
          uint8_t count = 0, raw = 0;
          if (reg_read(3, &count) || !(count & 15) || reg_read(4, &raw))
            break;
          unsigned code = raw & 0x7f;
          if ((raw & 0x80) && code && (code - 1) / 10 < 7 &&
              (code - 1) % 10 < 8) {
            unsigned index = ((code - 1) / 10) * 8 + (code - 1) % 10;
            atomic_fetch_or_explicit(&raw_keys[index / 32], 1u << (index % 32),
                                     memory_order_relaxed);
          }
          pos_event_t event;
          if (pos_keyboard_decode(&keys, raw, &event)) {
            atomic_fetch_add_explicit(&key_count, 1, memory_order_relaxed);
            enqueue(event);
          }
        }
        reg_write(2, 0x0f);
      }
    }
    int c = serial_input ? pos_console_poll(STDIN_FILENO) : -1;
    if (c != EOF) {
      pos_event_t e = {.pressed = true};
      e.key = c == 27              ? POS_KEY_ESCAPE
              : c == 29 || c == 0  ? POS_KEY_HOME
              : c == 10 || c == 13 ? POS_KEY_ENTER
              : c == 8 || c == 127 ? POS_KEY_BACKSPACE
                                   : c;
      enqueue(e);
    }
    bool down = gpio_get_level(GPIO_NUM_0) == 0;
    if (down && !button_down)
      enqueue((pos_event_t){POS_KEY_HOME, 0, true});
    button_down = down;
    bool system_held = down || (keys.held[28] && keys.held[55]);
    if (system_held) {
      if (!held_since)
        held_since = esp_timer_get_time();
      if (esp_timer_get_time() - held_since > 2000000) {
        force_recovery = RECOVERY_MAGIC;
        esp_restart();
      }
    } else
      held_since = 0;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void pos_device_brightness(unsigned percent) {
  if (percent > 100)
    percent = 100;
  if (backlight) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  }
}

int pos_device_init(void) {
  serial_input = pos_console_nonblocking(STDIN_FILENO) == 0;
  if (boot_magic != BOOT_MAGIC) {
    boot_magic = BOOT_MAGIC;
    boot_attempts = 0;
    force_recovery = 0;
  }
  ++boot_attempts;
  events = xQueueCreate(32, sizeof(pos_event_t));
  drawn = xSemaphoreCreateBinary();
  if (!events || !drawn)
    return -1;
  gpio_config_t button = {.pin_bit_mask = 1ULL << 0,
                          .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE};
  gpio_config(&button);
  ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                               .duty_resolution = LEDC_TIMER_10_BIT,
                               .timer_num = LEDC_TIMER_0,
                               .freq_hz = 1000,
                               .clk_cfg = LEDC_AUTO_CLK};
  ledc_channel_config_t light = {.gpio_num = 38,
                                 .speed_mode = LEDC_LOW_SPEED_MODE,
                                 .channel = LEDC_CHANNEL_0,
                                 .timer_sel = LEDC_TIMER_0,
                                 .duty = 0};
  backlight = ledc_timer_config(&timer) == ESP_OK &&
              ledc_channel_config(&light) == ESP_OK;
  spi_bus_config_t bus = {.mosi_io_num = 35,
                          .miso_io_num = -1,
                          .sclk_io_num = 36,
                          .quadwp_io_num = -1,
                          .quadhd_io_num = -1,
                          .max_transfer_sz = 240 * 8 * 2};
  stripe = heap_caps_malloc(240 * 8 * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (stripe &&
      spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) == ESP_OK) {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t cfg = {.cs_gpio_num = 37,
                                         .dc_gpio_num = 34,
                                         .spi_mode = 0,
                                         .pclk_hz = 20000000,
                                         .trans_queue_depth = 1,
                                         .on_color_trans_done = color_done,
                                         .lcd_cmd_bits = 8,
                                         .lcd_param_bits = 8};
    esp_lcd_panel_dev_config_t display = {.reset_gpio_num = 33,
                                          .rgb_ele_order =
                                              LCD_RGB_ELEMENT_ORDER_RGB,
                                          .bits_per_pixel = 16};
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg,
                                 &io) == ESP_OK &&
        esp_lcd_new_panel_st7789(io, &display, &panel) == ESP_OK &&
        esp_lcd_panel_reset(panel) == ESP_OK &&
        esp_lcd_panel_init(panel) == ESP_OK &&
        esp_lcd_panel_swap_xy(panel, true) == ESP_OK &&
        esp_lcd_panel_mirror(panel, false, true) == ESP_OK &&
        esp_lcd_panel_set_gap(panel, 40, 52) == ESP_OK &&
        esp_lcd_panel_invert_color(panel, true) == ESP_OK &&
        esp_lcd_panel_disp_on_off(panel, true) == ESP_OK)
      info.display_ok = true;
  }
  i2c_master_bus_handle_t i2c;
  i2c_master_bus_config_t pins = {.i2c_port = I2C_NUM_0,
                                  .sda_io_num = 8,
                                  .scl_io_num = 9,
                                  .clk_source = I2C_CLK_SRC_DEFAULT,
                                  .glitch_ignore_cnt = 7,
                                  .flags.enable_internal_pullup = true};
  i2c_device_config_t chip = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                              .device_address = 0x34,
                              .scl_speed_hz = 400000};
  if (i2c_new_master_bus(&pins, &i2c) == ESP_OK &&
      i2c_master_probe(i2c, 0x34, 50) == ESP_OK &&
      i2c_master_bus_add_device(i2c, &chip, &keyboard) == ESP_OK) {
    int bad = 0;
    for (int r = 0x23; r <= 0x2b; ++r)
      bad |= reg_write(r, 0);
    bad |= reg_write(0x1d, 0x7f);
    bad |= reg_write(0x1e, 0xff);
    bad |= reg_write(0x1f, 0);
    for (int i = 0; i < 16; ++i) {
      uint8_t raw = 0;
      if (reg_read(4, &raw) || !raw)
        break;
    }
    bad |= reg_write(2, 0x0f);
    bad |= reg_write(1, 0x09);
    info.keyboard_ok = !bad;
  }
  adc_oneshot_unit_init_cfg_t acfg = {.unit_id = ADC_UNIT_1};
  adc_oneshot_chan_cfg_t channel = {.bitwidth = ADC_BITWIDTH_DEFAULT,
                                    .atten = ADC_ATTEN_DB_12};
  if (adc_oneshot_new_unit(&acfg, &adc) == ESP_OK &&
      adc_oneshot_config_channel(adc, ADC_CHANNEL_9, &channel) == ESP_OK) {
    adc_cali_curve_fitting_config_t cal = {.unit_id = ADC_UNIT_1,
                                           .chan = ADC_CHANNEL_9,
                                           .atten = ADC_ATTEN_DB_12,
                                           .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cal, &calibration) != ESP_OK)
      calibration = NULL;
  }
  if (xTaskCreate(input_task, "pos_input", 4096, NULL, 5, NULL) != pdPASS)
    return -1;
  pos_device_brightness(70);
  return 0;
}

bool pos_device_event(pos_event_t *event, uint32_t timeout_ms) {
  bool received = events && xQueueReceive(events, event,
                                          pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  if (received && event->pressed && event->key == POS_KEY_HOME)
    pos_home_request();
  return received;
}

void pos_device_boot_flash(void) {
  /* Once per boot, immediately after initialization; no SD assets or
   * framebuffer. */
  if (!info.display_ok)
    return;
  pos_view_t view;
  uint32_t hold_ms;
  for (unsigned i = 0; pos_boot_frame(i, &view, &hold_ms); ++i) {
    int64_t start = esp_timer_get_time();
    pos_device_show(&view);
    if (!info.display_ok)
      break;
    /* Count transfer time toward animation frame pacing; still hold the
     * flashes and final splash for their full duration after presentation. */
    int64_t remaining = (int64_t)hold_ms * 1000;
    if (view.boot_splash && i != POS_BOOT_HOLD_FRAME)
      remaining -= esp_timer_get_time() - start;
    TickType_t ticks =
        remaining > 0 ? pdMS_TO_TICKS((remaining + 999) / 1000) : 0;
    vTaskDelay(ticks ? ticks : 1);
  }
}

void pos_device_show(const pos_view_t *v) {
  if (previous_valid && !memcmp(&previous, v, sizeof(*v)))
    return;
  previous = *v;
  previous_valid = true;
  if (!info.display_ok) {
    printf("\n-- %s --\n", v->lines[0]);
    for (int i = 1; i < POS_ROWS; ++i)
      if (*v->lines[i])
        printf("%s\n", v->lines[i]);
    return;
  }
  int64_t start = esp_timer_get_time();
  for (int y = 0; y < 135; y += 8) {
    int lines = 135 - y < 8 ? 135 - y : 8;
    for (int row = 0; row < lines; ++row)
      for (int x = 0; x < 240; ++x) {
        uint16_t c = pos_view_pixel(v, x, y + row);
        stripe[row * 240 + x] = (c << 8) | (c >> 8);
      }
    if (esp_lcd_panel_draw_bitmap(panel, 0, y, 240, y + lines, stripe) !=
            ESP_OK ||
        xSemaphoreTake(drawn, pdMS_TO_TICKS(1000)) != pdTRUE) {
      info.display_ok = false;
      previous_valid = false;
      break;
    }
  }
  info.frame_us = (uint32_t)(esp_timer_get_time() - start);
  if (info.display_ok) {
    if (!info.frames || info.frame_us < info.frame_min_us)
      info.frame_min_us = info.frame_us;
    if (info.frame_us > info.frame_max_us)
      info.frame_max_us = info.frame_us;
    ++info.frames;
    info.frame_total_us += info.frame_us;
  }
}

int pos_device_battery_mv(void) {
  if (!adc || !calibration)
    return 0;
  int total = 0;
  for (int i = 0; i < 8; ++i) {
    int raw, mv;
    if (adc_oneshot_read(adc, ADC_CHANNEL_9, &raw) != ESP_OK ||
        adc_cali_raw_to_voltage(calibration, raw, &mv) != ESP_OK)
      return 0;
    total += mv;
  }
  return total / 4; /* eight readings, published divider ratio 2 */
}
pos_device_info_t pos_device_info(void) {
  pos_device_info_t snapshot = info;
  if (wifi_initialized) {
    wifi_ap_record_t ap;
    snapshot.wifi_connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  }
  snapshot.keys = atomic_load_explicit(&key_count, memory_order_relaxed);
  snapshot.raw_keys[0] =
      atomic_load_explicit(&raw_keys[0], memory_order_relaxed);
  snapshot.raw_keys[1] =
      atomic_load_explicit(&raw_keys[1], memory_order_relaxed);
  snapshot.fifo_overflows =
      atomic_load_explicit(&fifo_overflows, memory_order_relaxed);
  snapshot.queue_drops =
      atomic_load_explicit(&queue_drops, memory_order_relaxed);
  return snapshot;
}
bool pos_device_safe_boot(void) {
  bool safe = force_recovery == RECOVERY_MAGIC || boot_attempts >= 3;
  force_recovery = 0;
  return safe;
}
void pos_device_boot_ready(uint32_t firmware_bytes) {
  info.firmware_bytes = firmware_bytes;
  boot_attempts = 0;
  info.boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void pos_device_wifi_stop(void) {
  if (wifi_initialized)
    esp_wifi_stop();
  info.wifi_connected = false;
  wifi_started = false;
}
int pos_device_wifi_prepare(void) {
  if (!wifi_initialized) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
      return -1;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
      return -1;
    if (!esp_netif_create_default_wifi_sta())
      return -1;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    if (esp_wifi_init(&init) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
      return -1;
    wifi_initialized = true;
  }
  if (!wifi_started) {
    if (esp_wifi_start() != ESP_OK)
      return -1;
    wifi_started = true;
  }
  return 0;
}
int pos_device_wifi(const char *ssid, const char *password,
                    uint32_t timeout_ms) {
  if (!ssid || !*ssid || strlen(ssid) > 32 || !password ||
      strlen(password) > 64)
    return -1;
  pos_device_wifi_stop();
  if (pos_device_wifi_prepare())
    return -1;
  wifi_config_t cfg = {0};
  memcpy(cfg.sta.ssid, ssid, strlen(ssid));
  memcpy(cfg.sta.password, password, strlen(password));
  cfg.sta.pmf_cfg.capable = true;
  int result = -1;
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) == ESP_OK &&
      esp_wifi_connect() == ESP_OK) {
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < (int64_t)timeout_ms * 1000) {
      esp_netif_t *net = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      esp_netif_ip_info_t ip;
      wifi_ap_record_t ap;
      if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
          !strncmp((const char *)ap.ssid, ssid, 32) && net &&
          esp_netif_get_ip_info(net, &ip) == ESP_OK && ip.ip.addr) {
        info.wifi_connected = true;
        result = 0;
        break;
      }
      pos_event_t event;
      if (pos_device_event(&event, 100) && event.pressed &&
          (event.key == POS_KEY_HOME || event.key == POS_KEY_ESCAPE))
        break;
    }
  }
  volatile unsigned char *bytes = (volatile unsigned char *)&cfg;
  for (size_t i = 0; i < sizeof(cfg); ++i)
    bytes[i] = 0;
  if (result)
    pos_device_wifi_stop();
  return result;
}
void pos_device_sleep(void) {
  pos_device_wifi_stop();
  pos_device_brightness(0);
  if (info.display_ok)
    esp_lcd_panel_disp_on_off(panel, false);
  esp_sleep_enable_ext1_wakeup_io(1ULL << 0, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

void pos_device_keyboard_begin(void) {
  atomic_store_explicit(&raw_keys[0], 0, memory_order_relaxed);
  atomic_store_explicit(&raw_keys[1], 0, memory_order_relaxed);
}
