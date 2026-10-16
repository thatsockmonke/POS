#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  char display_name[49];
  char device_name[25];
  char timezone[64];
  uint8_t brightness;
  bool setup_complete;
  uint32_t generation;
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
