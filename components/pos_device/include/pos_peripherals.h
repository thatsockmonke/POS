#pragma once
#include "pos_install.h"
#include "pos_radio.h"
int pos_audio_write(const int16_t *stereo, size_t frames, unsigned sample_rate,
                    pos_error_t *e);
void pos_audio_stop(void);
typedef enum {
  POS_SOUND_KEY,
  POS_SOUND_NAV,
  POS_SOUND_SELECT,
  POS_SOUND_BACK,
  POS_SOUND_OPEN,
  POS_SOUND_CLOSE,
  POS_SOUND_NOTICE,
  POS_SOUND_PREVIEW,
  POS_SOUND_STARTUP,
  POS_SOUND_COUNT
} pos_sound_t;
/* One coalesced asynchronous cue; never mixes with playback or microphone. */
int pos_audio_feedback_configure(bool enabled, unsigned volume, bool keys,
                                 bool alerts);
void pos_audio_feedback(pos_sound_t sound);
void pos_audio_feedback_shutdown(void);
/* Nestable temporary UI-cue pause, without changing saved sound settings. */
void pos_audio_feedback_hold(bool hold);
bool pos_microphone_active(void);
size_t pos_microphone_pending(void);
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
/* Lightweight GPS snapshot: map polling does not copy radio queues. */
pos_location_t pos_expansion_location(void);
int pos_expansion_send(uint8_t type, uint32_t destination, const char *text,
                       bool share_location, pos_error_t *e);
int pos_expansion_retry(pos_error_t *e);
int pos_expansion_range(bool enable, pos_error_t *e);
void pos_expansion_stop(void);

#include "pos_instruments.h"
/* Audio is exclusive: microphone and speaker cannot run together. */
int pos_microphone_start(pos_error_t *);
/* Live integer digital PCM gain, 1-4096; does not write flash. */
int pos_microphone_set_gain(unsigned gain);
int pos_microphone_read(int16_t *mono, size_t capacity, size_t *frames,
                        pos_error_t *);
/* Stop acquisition, retaining queued PCM. Read until frames == 0, then stop
 * to free the channel/buffer. This preserves the tail on pause and save. */
void pos_microphone_end(void);
/* Raw I2S peaks since the previous read, before gain; RIGHT feeds the WAV. */
void pos_microphone_levels(unsigned *left, unsigned *right);
void pos_microphone_stop(void);
int pos_motion_start(pos_error_t *);
int pos_motion_read(pos_imu_sample_t *, pos_error_t *);
void pos_motion_stop(void);
int pos_infrared_send(uint8_t address, uint8_t command, pos_error_t *);
