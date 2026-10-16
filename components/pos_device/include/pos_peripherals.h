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
