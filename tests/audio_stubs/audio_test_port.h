#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define IRAM_ATTR
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
typedef struct test_stream *StreamBufferHandle_t;
typedef struct test_sem *SemaphoreHandle_t;
typedef void *i2s_chan_handle_t;
typedef struct {
  int unused;
} i2s_event_data_t;
typedef struct {
  int auto_clear, dma_desc_num, dma_frame_num;
} i2s_chan_config_t;
typedef struct {
  int clk_cfg, slot_cfg;
  struct {
    int mclk, bclk, ws, dout, din;
  } gpio_cfg;
} i2s_std_config_t;
typedef struct {
  bool (*on_recv_q_ovf)(i2s_chan_handle_t, i2s_event_data_t *, void *);
} i2s_event_callbacks_t;
#define I2S_NUM_1 1
#define I2S_ROLE_MASTER 1
#define I2S_GPIO_UNUSED -1
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
#define I2S_CHANNEL_DEFAULT_CONFIG(a, b) ((i2s_chan_config_t){0})
#define I2S_STD_CLK_DEFAULT_CONFIG(a) (a)
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(a, b) (b)
int i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *,
                    i2s_chan_handle_t *);
int i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
int i2s_channel_register_event_callback(i2s_chan_handle_t,
                                        const i2s_event_callbacks_t *, void *);
int i2s_channel_enable(i2s_chan_handle_t);
int i2s_channel_disable(i2s_chan_handle_t);
int i2s_del_channel(i2s_chan_handle_t);
int i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, unsigned);
int i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *,
                      unsigned);
StreamBufferHandle_t xStreamBufferCreate(size_t, size_t);
size_t xStreamBufferSend(StreamBufferHandle_t, const void *, size_t, unsigned);
size_t xStreamBufferReceive(StreamBufferHandle_t, void *, size_t, unsigned);
void vStreamBufferDelete(StreamBufferHandle_t);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
void vSemaphoreDelete(SemaphoreHandle_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned,
                void *);
void vTaskDelete(void *);

void vTaskDelay(unsigned);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t);
