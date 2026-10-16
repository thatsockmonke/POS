#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pos_capabilities.h"
#include "pos_device.h"
#include "pos_files.h"
#include "pos_peripherals.h"
#include "vendor/sx126x/sx126x.h"
#include "vendor/sx126x/sx126x_hal.h"
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static spi_device_handle_t radio_spi;
static SemaphoreHandle_t snapshots;
static QueueHandle_t commands;
static TaskHandle_t worker;
static atomic_bool enabled, transmit, stopping;
static atomic_bool running;
static atomic_uint frequency;
static pos_expansion_status_t hw, published;
static pos_nmea_t gps;
static char storage[POS_FS_PATH];
static FILE *range_file;
typedef struct {
  char command;
  bool enable;
  pos_radio_packet_t packet;
} request_t;
static sx126x_mod_params_lora_t modulation = {.sf = SX126X_LORA_SF7,
                                              .bw = SX126X_LORA_BW_125,
                                              .cr = SX126X_LORA_CR_4_5,
                                              .ldro = 0};
static sx126x_pkt_params_lora_t parameters = {.preamble_len_in_symb = 12,
                                              .header_type =
                                                  SX126X_LORA_PKT_EXPLICIT,
                                              .pld_len_in_bytes = 255,
                                              .crc_is_on = true,
                                              .invert_iq_is_on = false};
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static int fail(pos_error_t *e, const char *message) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", message);
  return -1;
}
static bool busy_ready(void) {
  uint64_t until = now_ms() + 250;
  while (gpio_get_level(6)) {
    if (now_ms() >= until)
      return false;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}
static sx126x_hal_status_t transfer(const uint8_t *command,
                                    uint16_t command_length,
                                    const uint8_t *data, uint8_t *reply,
                                    uint16_t size, bool wake) {
  if (!radio_spi || command_length + size > 272 || (!wake && !busy_ready()))
    return SX126X_HAL_STATUS_ERROR;
  uint8_t tx[272] = {0}, rx[272] = {0};
  memcpy(tx, command, command_length);
  if (data)
    memcpy(tx + command_length, data, size);
  spi_transaction_t transaction = {
      .length = (command_length + size) * 8, .tx_buffer = tx, .rx_buffer = rx};
  if (spi_device_transmit(radio_spi, &transaction) != ESP_OK || !busy_ready())
    return SX126X_HAL_STATUS_ERROR;
  if (reply)
    memcpy(reply, rx + command_length, size);
  return SX126X_HAL_STATUS_OK;
}
sx126x_hal_status_t sx126x_hal_write(const void *ctx, const uint8_t *cmd,
                                     uint16_t length, const uint8_t *data,
                                     uint16_t size) {
  (void)ctx;
  return transfer(cmd, length, data, NULL, size, false);
}
sx126x_hal_status_t sx126x_hal_read(const void *ctx, const uint8_t *cmd,
                                    uint16_t length, uint8_t *data,
                                    uint16_t size) {
  (void)ctx;
  return transfer(cmd, length, NULL, data, size, false);
}
sx126x_hal_status_t sx126x_hal_reset(const void *ctx) {
  (void)ctx;
  gpio_set_level(3, 0);
  vTaskDelay(pdMS_TO_TICKS(2));
  gpio_set_level(3, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  return busy_ready() ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}
sx126x_hal_status_t sx126x_hal_wakeup(const void *ctx) {
  (void)ctx;
  uint8_t cmd[] = {0xC0, 0};
  return transfer(cmd, sizeof(cmd), NULL, NULL, 0, true);
}
static int antenna_switch(void) {
  uint8_t id, value;
  if (pos_device_i2c_read(0x43, 1, &id) || !id || id == 255)
    return -1;
  const uint8_t regs[] = {3, 5, 7};
  for (unsigned i = 0; i < 3; ++i) {
    if (pos_device_i2c_read(0x43, regs[i], &value) ||
        pos_device_i2c_write(0x43, regs[i],
                             regs[i] == 7 ? (value & ~1) : (value | 1)))
      return -1;
  }
  return 0;
}
static int probe_cap(void) {
  if (antenna_switch())
    return -1;
  if (!radio_spi) {
    gpio_config_t pins = {.pin_bit_mask = (1ULL << 3),
                          .mode = GPIO_MODE_OUTPUT};
    gpio_config(&pins);
    gpio_set_level(3, 1);
    pins = (gpio_config_t){.pin_bit_mask = (1ULL << 6) | (1ULL << 4),
                           .mode = GPIO_MODE_INPUT,
                           .pull_down_en = GPIO_PULLDOWN_ENABLE};
    gpio_config(&pins);
    spi_device_interface_config_t config = {.clock_speed_hz = 2000000,
                                            .mode = 0,
                                            .spics_io_num = 5,
                                            .queue_size = 1};
    if (spi_bus_add_device(SPI3_HOST, &config, &radio_spi) != ESP_OK)
      return -1;
  }
  sx126x_pkt_type_t type;
  return sx126x_hal_reset(NULL) ||
                 sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC) ||
                 sx126x_set_pkt_type(NULL, SX126X_PKT_TYPE_LORA) ||
                 sx126x_get_pkt_type(NULL, &type) ||
                 type != SX126X_PKT_TYPE_LORA
             ? -1
             : 0;
}
static int radio_receive_mode(void) {
  parameters.pld_len_in_bytes = 255;
  return sx126x_set_lora_pkt_params(NULL, &parameters) ||
                 sx126x_set_rx_with_timeout_in_rtc_step(NULL,
                                                        SX126X_RX_CONTINUOUS)
             ? -1
             : 0;
}
static int configure_radio(uint32_t hz) {
  sx126x_pa_cfg_params_t pa = {
      .pa_duty_cycle = 4, .hp_max = 7, .device_sel = 0, .pa_lut = 1};
  uint8_t sync[] = {
      0x14, 0x24}; /* Private LoRa sync word, deliberately not Meshtastic. */
  return sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC) ||
                 sx126x_set_dio3_as_tcxo_ctrl(NULL, SX126X_TCXO_CTRL_3_0V,
                                              640) ||
                 sx126x_cal(NULL, SX126X_CAL_ALL) ||
                 sx126x_cal_img_in_mhz(NULL, hz < 900000000 ? 863 : 902,
                                       hz < 900000000 ? 870 : 928) ||
                 sx126x_set_reg_mode(NULL, SX126X_REG_MODE_DCDC) ||
                 sx126x_set_pa_cfg(NULL, &pa) ||
                 sx126x_set_dio2_as_rf_sw_ctrl(NULL, true) ||
                 sx126x_set_rf_freq(NULL, hz) ||
                 sx126x_set_tx_params(NULL, 10, SX126X_RAMP_200_US) ||
                 sx126x_set_buffer_base_address(NULL, 0, 0) ||
                 sx126x_set_lora_mod_params(NULL, &modulation) ||
                 sx126x_write_register(NULL, 0x0740, sync, 2) ||
                 sx126x_set_dio_irq_params(
                     NULL, SX126X_IRQ_ALL,
                     SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE, 0, 0) ||
                 sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL) ||
                 radio_receive_mode()
             ? -1
             : 0;
}
static int outbox_path(uint32_t id, char path[POS_FS_PATH]) {
  char relative[128];
  snprintf(relative, sizeof(relative), "POS/Users/Owner/Radio/outbox/%08x.posr",
           (unsigned)id);
  return pos_fs_join(path, storage, relative);
}
static void record(const char *event, const pos_radio_packet_t *p, int rssi,
                   float snr) {
  hw.location = pos_location_snapshot(&gps, now_ms());
  if (range_file && pos_range_row(range_file, event, p, &hw.location, rssi, snr,
                                  &hw.radio, now_ms())) {
    fclose(range_file);
    range_file = NULL;
    hw.logging = false;
    snprintf(hw.error, sizeof(hw.error),
             "Range log write failed; existing log retained.");
  }
}
static void range_start(void) {
  if (range_file)
    return;
  char folder[POS_FS_PATH];
  pos_fs_join(folder, storage, "POS/Users/Owner/RangeTests");
  if (pos_fs_mkdirs(folder)) {
    snprintf(hw.error, sizeof(hw.error), "Cannot create range log folder.");
    return;
  }
  for (unsigned i = 0; i < 100000; ++i) {
    snprintf(hw.log_path, sizeof(hw.log_path),
             "POS/Users/Owner/RangeTests/range-%u.csv", i);
    char path[POS_FS_PATH];
    pos_fs_join(path, storage, hw.log_path);
    range_file = fopen(path, "wx");
    if (range_file || errno != EEXIST)
      break;
  }
  if (!range_file || pos_range_header(range_file)) {
    if (range_file)
      fclose(range_file);
    range_file = NULL;
    snprintf(hw.error, sizeof(hw.error), "Cannot create range log.");
    return;
  }
  hw.logging = true;
  record("SAMPLE", NULL, 0, 0);
}
static void load_outbox(void) {
  char folder[POS_FS_PATH];
  pos_fs_join(folder, storage, "POS/Users/Owner/Radio/outbox");
  DIR *d = opendir(folder);
  if (!d)
    return;
  struct dirent *entry;
  while ((entry = readdir(d))) {
    size_t name_length = strlen(entry->d_name);
    if (name_length < 5 || strcmp(entry->d_name + name_length - 5, ".posr"))
      continue;
    char path[POS_FS_PATH];
    if (pos_fs_join(path, folder, entry->d_name))
      continue;
    FILE *f = fopen(path, "rb");
    if (!f)
      continue;
    uint8_t wire[POS_RADIO_WIRE_MAX + 1];
    size_t n = fread(wire, 1, sizeof(wire), f);
    fclose(f);
    pos_radio_packet_t packet;
    if (!pos_radio_decode(wire, n, &packet))
      pos_radio_enqueue(&hw.radio, &packet, now_ms());
  }
  closedir(d);
}
static int write_received(const pos_radio_packet_t *p) {
  char path[POS_FS_PATH];
  pos_fs_join(path, storage, "POS/Users/Owner/Radio/messages.jsonl");
  /* Wire text is printable ASCII; escape JSON quotes/backslashes explicitly. */
  FILE *f = fopen(path, "ab");
  if (!f) {
    snprintf(hw.error, sizeof(hw.error), "Cannot save received message.");
    return -1;
  }
  int bad =
      fprintf(f, "{\"from\":\"%08x\",\"id\":\"%08x\",\"type\":%u,\"text\":\"",
              (unsigned)p->sender, (unsigned)p->id, p->type) < 0;
  for (const char *c = p->text; *c && !bad; ++c) {
    if ((*c == '"' || *c == '\\') && fputc('\\', f) == EOF)
      bad = 1;
    if (fputc(*c, f) == EOF)
      bad = 1;
  }
  if (fputs("\"}\n", f) == EOF || fflush(f) || fsync(fileno(f)))
    bad = 1;
  if (fclose(f))
    bad = 1;
  if (bad)
    snprintf(hw.error, sizeof(hw.error),
             "Message log write failed; partial log retained.");
  return bad ? -1 : 0;
}
static int send_wire(const pos_radio_packet_t *packet, uint32_t *airtime) {
  uint8_t wire[POS_RADIO_WIRE_MAX];
  size_t n = pos_radio_encode(packet, wire);
  if (!n)
    return -1;
  parameters.pld_len_in_bytes = n;
  *airtime = sx126x_get_lora_time_on_air_in_ms(&parameters, &modulation);
  if (sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC) ||
      sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL) ||
      sx126x_set_lora_pkt_params(NULL, &parameters) ||
      sx126x_write_buffer(NULL, 0, wire, n) || sx126x_set_tx(NULL, 2000))
    return -1;
  uint64_t limit = now_ms() + 2100;
  int result = -1;
  while (now_ms() < limit) {
    sx126x_irq_mask_t irq;
    if (sx126x_get_irq_status(NULL, &irq))
      break;
    if (irq & SX126X_IRQ_TX_DONE) {
      result = 0;
      break;
    }
    if (irq & SX126X_IRQ_TIMEOUT)
      break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);
  radio_receive_mode();
  return result;
}
static void publish(void) {
  xSemaphoreTake(snapshots, portMAX_DELAY);
  published = hw;
  xSemaphoreGive(snapshots);
}
static void run(void *unused) {
  (void)unused;
  uint64_t next_probe = 0, next_tx = 0, next_sample = 0;
  uint32_t configured = 0;
  pos_radio_packet_t ack_queue[4] = {0};
  unsigned ack_count = 0;
  uint8_t bytes[256];
  uart_config_t config = {.baud_rate = 115200,
                          .data_bits = UART_DATA_8_BITS,
                          .parity = UART_PARITY_DISABLE,
                          .stop_bits = UART_STOP_BITS_1,
                          .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                          .source_clk = UART_SCLK_DEFAULT};
  bool uart_installed =
      uart_driver_install(UART_NUM_1, 4096, 0, 0, NULL, 0) == ESP_OK;
  bool uart_ok = uart_installed &&
                 uart_param_config(UART_NUM_1, &config) == ESP_OK &&
                 uart_set_pin(UART_NUM_1, 13, 15, UART_PIN_NO_CHANGE,
                              UART_PIN_NO_CHANGE) == ESP_OK;
  if (uart_installed && !uart_ok)
    uart_driver_delete(UART_NUM_1);
  if (!uart_ok)
    snprintf(hw.error, sizeof(hw.error), "GPS UART could not start.");
  while (!atomic_load(&stopping)) {
    uint64_t now = now_ms();
    if (uart_ok) {
      int n = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), 0);
      if (n > 0)
        pos_nmea_feed(&gps, bytes, n, now);
    }
    hw.location = pos_location_snapshot(&gps, now);
    hw.gps_detected = gps.sentences && now >= gps.sentence_ms &&
                      now - gps.sentence_ms < 10000;
    hw.maps_available =
        pos_feature_allowed(POS_FEATURE_MAPS, hw.cap_detected, hw.gps_detected,
                            atomic_load(&enabled));
    hw.lora_enabled = atomic_load(&enabled);
    hw.tx_enabled = atomic_load(&transmit);
    hw.frequency_hz = atomic_load(&frequency);
    if (now >= next_probe) {
      if (!hw.cap_detected) {
        hw.cap_detected = probe_cap() == 0;
        configured = 0;
      } else {
        sx126x_pkt_type_t type;
        if (sx126x_get_pkt_type(NULL, &type) || type != SX126X_PKT_TYPE_LORA) {
          hw.cap_detected = false;
          configured = 0;
          hw.radio_ready = false;
        }
      }
      next_probe = now + 5000;
    }
    if (!hw.lora_enabled || !hw.cap_detected) {
      if (hw.radio_ready) {
        sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC);
        configured = 0;
      }
      hw.radio_ready = false;
      ack_count = 0;
    } else if (configured != hw.frequency_hz) {
      hw.radio_ready = configure_radio(hw.frequency_hz) == 0;
      configured = hw.radio_ready ? hw.frequency_hz : 0;
      if (!hw.radio_ready)
        snprintf(hw.error, sizeof(hw.error),
                 "LoRa setup failed; check cap and antenna routing.");
    }
    request_t request;
    while (xQueueReceive(commands, &request, 0) == pdTRUE) {
      if (request.command == 'S') {
        if (!hw.lora_enabled || !hw.tx_enabled ||
            pos_radio_enqueue(&hw.radio, &request.packet, now))
          snprintf(hw.error, sizeof(hw.error),
                   "Message saved in outbox; TX disabled or queue full. Retry "
                   "later.");
      } else if (request.command == 'R') {
        if (request.enable && hw.lora_enabled && hw.radio_ready)
          range_start();
        else if (range_file) {
          fclose(range_file);
          range_file = NULL;
          hw.logging = false;
        }
      } else if (request.command == 'C')
        load_outbox();
    }
    if (!hw.lora_enabled && range_file) {
      fclose(range_file);
      range_file = NULL;
      hw.logging = false;
    }
    if (hw.radio_ready) {
      sx126x_irq_mask_t irq;
      if (!sx126x_get_irq_status(NULL, &irq) && (irq & SX126X_IRQ_RX_DONE)) {
        sx126x_rx_buffer_status_t buffer;
        sx126x_pkt_status_lora_t signal;
        pos_radio_packet_t packet, ack;
        if (!(irq & SX126X_IRQ_CRC_ERROR) &&
            !sx126x_get_rx_buffer_status(NULL, &buffer) &&
            !sx126x_read_buffer(NULL, buffer.buffer_start_pointer, bytes,
                                buffer.pld_len_in_bytes) &&
            !pos_radio_decode(bytes, buffer.pld_len_in_bytes, &packet) &&
            !sx126x_get_lora_pkt_status(NULL, &signal)) {
          hw.rssi = signal.rssi_pkt_in_dbm;
          hw.snr = signal.snr_pkt_in_db;
          uint32_t previously_acked = hw.radio.acked;
          int result = pos_radio_receive(&hw.radio, &packet, &ack);
          if (result == 1) {
            if (packet.type != POS_RADIO_PING && write_received(&packet)) {
              for (unsigned i = 0; i < 32; ++i)
                if (hw.radio.seen[i].sender == packet.sender &&
                    hw.radio.seen[i].id == packet.id)
                  hw.radio.seen[i] = (pos_radio_seen_t){0};
              if (hw.radio.received)
                --hw.radio.received;
              ack.type = 0;
              result = -1;
            } else
              snprintf(hw.last_message, sizeof(hw.last_message), "%08x: %s",
                       (unsigned)packet.sender, packet.text);
          }
          if (result >= 0) {
            record(packet.type == POS_RADIO_ACK ? "ACK" : "RX", &packet,
                   hw.rssi, hw.snr);
            if (ack.type && hw.tx_enabled && ack_count < 4)
              ack_queue[ack_count++] = ack;
          }
          if (hw.radio.acked != previously_acked) {
            char path[POS_FS_PATH];
            outbox_path(packet.id, path);
            (void)unlink(path);
          }
        }
        sx126x_clear_irq_status(NULL, SX126X_IRQ_ALL);
      }
      if (hw.tx_enabled && now >= next_tx) {
        pos_radio_packet_t packet;
        int due;
        if (ack_count) {
          packet = ack_queue[--ack_count];
          due = 1;
        } else
          due = pos_radio_due(&hw.radio, now, &packet);
        if (due == 1) {
          uint32_t airtime = 0;
          int result = send_wire(&packet, &airtime);
          next_tx = now_ms() + (uint64_t)(airtime ? airtime : 1000) * 100;
          if (!result) {
            record("TX", &packet, 0, 0);
            if (!packet.destination) {
              char path[POS_FS_PATH];
              outbox_path(packet.id, path);
              (void)unlink(path);
            }
          } else
            snprintf(hw.error, sizeof(hw.error),
                     "LoRa transmit failed; outbox retained.");
        } else if (due < 0)
          record("TIMEOUT", &packet, 0, 0);
      }
    }
    if (range_file && now >= next_sample) {
      record("SAMPLE", NULL, 0, 0);
      next_sample = now + 1000;
    }
    publish();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
  if (range_file) {
    fclose(range_file);
    range_file = NULL;
  }
  if (radio_spi) {
    sx126x_set_standby(NULL, SX126X_STANDBY_CFG_RC);
    spi_bus_remove_device(radio_spi);
    radio_spi = NULL;
  }
  if (uart_ok)
    uart_driver_delete(UART_NUM_1);
  worker = NULL;
  atomic_store(&running, false);
  vTaskDelete(NULL);
}
int pos_expansion_init(const char *root) {
  if (worker)
    return 0;
  if (strlen(root) >= sizeof(storage))
    return -1;
  strcpy(storage, root);
  snapshots = xSemaphoreCreateMutex();
  commands = xQueueCreate(8, sizeof(request_t));
  if (!snapshots || !commands) {
    if (snapshots)
      vSemaphoreDelete(snapshots);
    if (commands)
      vQueueDelete(commands);
    snapshots = NULL;
    commands = NULL;
    return -1;
  }
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint32_t node = pos_crc32(0, mac, sizeof(mac));
  if (!node)
    node = 1;
  memset(&hw, 0, sizeof(hw));
  hw.node = hw.radio.node = node;
  atomic_store(&enabled, false);
  atomic_store(&transmit, false);
  atomic_store(&frequency, 915000000);
  nvs_handle_t h;
  uint64_t policy;
  if (nvs_flash_init() == ESP_OK &&
      nvs_open("pos_hardware", NVS_READONLY, &h) == ESP_OK) {
    if (nvs_get_u64(h, "policy", &policy) == ESP_OK) {
      uint32_t hz = policy >> 32;
      if (hz >= 868000000 && hz <= 923000000) {
        atomic_store(&enabled, policy & 1);
        atomic_store(&frequency, hz);
      }
    }
    nvs_close(h);
  }
  char path[POS_FS_PATH];
  pos_fs_join(path, root, "POS/Users/Owner/Radio/outbox");
  if (pos_fs_mkdirs(path))
    snprintf(hw.error, sizeof(hw.error), "Cannot create radio outbox.");
  atomic_store(&stopping, false);
  publish();
  atomic_store(&running, true);
  if (xTaskCreate(run, "pos_expansion", 8192, NULL, 4, &worker) != pdPASS) {
    atomic_store(&running, false);
    snprintf(hw.error, sizeof(hw.error),
             "Expansion task could not start; check heap.");
    publish();
    return -1;
  }
  return 0;
}
pos_expansion_status_t pos_expansion_status(void) {
  pos_expansion_status_t s = {0};
  if (snapshots) {
    xSemaphoreTake(snapshots, portMAX_DELAY);
    s = published;
    xSemaphoreGive(snapshots);
  } else
    snprintf(s.error, sizeof(s.error), "Expansion service unavailable.");
  return s;
}
bool pos_expansion_maps_available(void) {
  return pos_expansion_status().gps_detected;
}
int pos_expansion_configure(bool allow, uint32_t hz, bool tx, pos_error_t *e) {
  if (hz < 868000000 || hz > 923000000)
    return fail(
        e, "Use a permitted frequency within the cap's 868-923 MHz range.");
  if (allow && !pos_expansion_status().cap_detected)
    return fail(e, "LoRa cap not detected. Features remain disabled.");
  atomic_store(&frequency, hz);
  atomic_store(&enabled, allow);
  atomic_store(&transmit, allow && tx);
  nvs_handle_t handle;
  if (nvs_flash_init() != ESP_OK ||
      nvs_open("pos_hardware", NVS_READWRITE, &handle) != ESP_OK)
    return fail(
        e, "Applied for this session; hardware preference could not be saved.");
  uint64_t policy = (uint64_t)hz << 32 | (allow ? 1 : 0);
  int result = nvs_set_u64(handle, "policy", policy) != ESP_OK ||
               nvs_commit(handle) != ESP_OK;
  nvs_close(handle);
  return result ? fail(e, "Applied for this session; previous saved preference "
                          "retained.")
                : 0;
}
int pos_expansion_send(uint8_t type, uint32_t destination, const char *text,
                       bool location, pos_error_t *e) {
  pos_expansion_status_t status = pos_expansion_status();
  if (!status.lora_enabled || !status.cap_detected || !status.tx_enabled)
    return fail(e, "Enable LoRa features and explicitly confirm antenna/TX in "
                   "Settings first.");
  if (!text || strlen(text) > 160)
    return fail(e, "Radio messages are limited to 160 ASCII characters.");
  request_t r = {.command = 'S',
                 .packet = {.type = type,
                            .sender = status.node,
                            .destination = destination,
                            .id = esp_random()}};
  if (!r.packet.id)
    r.packet.id = 1;
  if (type == POS_RADIO_LOC) {
    if (!location || !status.location.fix)
      return fail(
          e, "Location sharing needs explicit consent and a current GPS fix.");
    snprintf(r.packet.text, sizeof(r.packet.text), "%.7f,%.7f",
             status.location.latitude, status.location.longitude);
  } else
    snprintf(r.packet.text, sizeof(r.packet.text), "%s", text);
  uint8_t wire[POS_RADIO_WIRE_MAX];
  if (!pos_radio_encode(&r.packet, wire))
    return fail(e, "Use printable ASCII radio text.");
  if (pos_radio_store(storage, &r.packet, e))
    return -1;
  return commands && xQueueSend(commands, &r, 0) == pdTRUE
             ? 0
             : fail(e, "Message saved in outbox; queue full. Choose Retry "
                       "saved outbox later.");
}
int pos_expansion_retry(pos_error_t *e) {
  request_t r = {.command = 'C'};
  return commands && xQueueSend(commands, &r, 0) == pdTRUE
             ? 0
             : fail(e, "Radio queue unavailable.");
}
int pos_expansion_range(bool enable, pos_error_t *e) {
  pos_expansion_status_t s = pos_expansion_status();
  if (enable && (!s.cap_detected || !s.lora_enabled || !s.radio_ready))
    return fail(e, "Enable a detected LoRa cap in Settings first.");
  request_t r = {.command = 'R', .enable = enable};
  return commands && xQueueSend(commands, &r, 0) == pdTRUE
             ? 0
             : fail(e, "Range logger unavailable.");
}
void pos_expansion_stop(void) {
  atomic_store(&transmit, false);
  atomic_store(&enabled, false);
  atomic_store(&stopping, true);
  uint64_t deadline = now_ms() + 3500;
  while (atomic_load(&running) && now_ms() < deadline)
    vTaskDelay(pdMS_TO_TICKS(10));
}
