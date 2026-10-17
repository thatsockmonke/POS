#pragma once
#include "pos_background.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  char display_name[49];
  char device_name[25];
  char timezone[64];
  uint8_t brightness;
  uint16_t
      microphone_gain; /* Integer 1x-4096x; older settings default to 8x. */
  bool sounds_enabled, sounds_keys, sounds_alerts;
  uint8_t sounds_volume;
  bool setup_complete;
  uint32_t generation;
  pos_background_t background;
} pos_settings_t;
void pos_settings_defaults(pos_settings_t *settings);
bool pos_settings_valid(const pos_settings_t *settings);
bool pos_settings_display_name_valid(const char *name);
bool pos_settings_device_name_valid(const char *name);
/* 0 loaded, 1 absent, -1 corrupt, -2 unknown future format. No files are
 * modified. */
int pos_settings_load(const char *root, pos_settings_t *settings);
int pos_settings_save(const char *root, pos_settings_t *settings);

int pos_settings_decode(const char *text, size_t length,
                        pos_settings_t *settings);
