#pragma once
#include "pos_install.h"
#include "pos_radio.h"
int pos_audio_write(const int16_t *stereo, size_t frames, unsigned sample_rate,
                    pos_error_t *e);
void pos_audio_stop(void);
typedef struct {
  bool cap_detected, gps_detected, lora_enabled, radio_ready, tx_enabled,
      maps_available, logging;
  uint32_t frequency_hz, node;
  pos_location_t location;
  pos_radio_state_t radio;
  int rssi;
  float snr;
  char last_message[200], log_path[241], error[128];
} pos_expansion_status_t;
/* GPS presence is based on valid receiver sentences, separately from fix. */
int pos_expansion_init(const char *root);
pos_expansion_status_t pos_expansion_status(void);
int pos_expansion_configure(bool enable, uint32_t frequency_hz, bool transmit,
                            pos_error_t *e);
bool pos_expansion_maps_available(void);
int pos_expansion_send(uint8_t type, uint32_t destination, const char *text,
                       bool share_location, pos_error_t *e);
int pos_expansion_retry(pos_error_t *e);
int pos_expansion_range(bool enable, pos_error_t *e);
void pos_expansion_stop(void);

#include "pos_instruments.h"
/* Audio is exclusive: microphone and speaker cannot run together. */
int pos_microphone_start(pos_error_t *);
int pos_microphone_read(int16_t *mono,size_t capacity,size_t *frames,pos_error_t *);
/* Stop acquisition, retaining queued PCM. Read until frames == 0, then stop
 * to free the channel/buffer. This preserves the tail on pause and save. */
void pos_microphone_end(void);
/* Raw I2S peaks since the previous read, before gain; RIGHT feeds the WAV. */
void pos_microphone_levels(unsigned *left, unsigned *right);
void pos_microphone_stop(void);
int pos_motion_start(pos_error_t *);
int pos_motion_read(pos_imu_sample_t *,pos_error_t *);
void pos_motion_stop(void);
int pos_infrared_send(uint8_t address,uint8_t command,pos_error_t *);
