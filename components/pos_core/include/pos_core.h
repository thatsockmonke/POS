#pragma once
#include "pos_background.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void pos_home_request(void);
bool pos_home_pending(void);
bool pos_home_take(void);

enum {
  POS_KEY_ENTER = 256,
  POS_KEY_ESCAPE,
  POS_KEY_BACKSPACE,
  POS_KEY_UP,
  POS_KEY_DOWN,
  POS_KEY_LEFT,
  POS_KEY_RIGHT,
  POS_KEY_HOME
};
enum { POS_MOD_SHIFT = 1, POS_MOD_CTRL = 2, POS_MOD_FN = 4, POS_MOD_ALT = 8 };
typedef struct {
  int key;
  unsigned modifiers;
  bool pressed;
} pos_event_t;
typedef struct {
  bool held[56];
  bool home_held;
} pos_keyboard_t;
/* TCA8418 FIFO event: low seven bits are 1 + row*10 + column, bit 7 is down. */
bool pos_keyboard_decode(pos_keyboard_t *keyboard, uint8_t raw,
                         pos_event_t *event);
int pos_navigation(
    const pos_event_t *event); /* context-specific WASD mapping */

#define POS_ROWS 16
#define POS_COLS 30
enum {
  POS_BOOT_COLOR_COUNT = 3,
  POS_BOOT_COLOR_MS = 180,
  POS_BOOT_SPLASH_MS = 1000,
  POS_BOOT_FRAME_MS = 40,
  POS_BOOT_SLIDE_FRAMES = 10,
  POS_BOOT_STAGGER_FRAMES = 5,
  POS_BOOT_FADE_FRAMES = 10,
  POS_BOOT_HOLD_FRAME = POS_BOOT_COLOR_COUNT + POS_BOOT_SLIDE_FRAMES +
                        POS_BOOT_STAGGER_FRAMES + POS_BOOT_FADE_FRAMES
};
extern const uint16_t pos_boot_colors[POS_BOOT_COLOR_COUNT];
typedef struct {
  char lines[POS_ROWS][POS_COLS + 1];
  int selected_row;
  bool tiles;
  int selected_tile;
  uint8_t icons[3][8];
  bool solid_color;
  uint16_t color_rgb565;
  bool boot_splash;
  uint16_t boot_pink_width;
  uint16_t boot_yellow_width;
  uint8_t boot_text_alpha;
  pos_background_t background;
  bool color_picker;
  uint16_t picker_hue, picker_saturation, picker_value;
  uint32_t picker_rgb;
  bool battery_overlay, battery_available, battery_full;
  uint8_t battery_percent, battery_bars;
  uint32_t editor_selected[12];
  bool text_editor;
  uint8_t editor_cursor_row, editor_cursor_col;
} pos_view_t;
void pos_view_clear(pos_view_t *view, const char *title);
void pos_view_line(pos_view_t *view, int row, const char *format, ...);
uint16_t pos_view_pixel(const pos_view_t *view, int x, int y);
void pos_color_picker_view(const char *title, uint32_t rgb, uint16_t hue,
                            uint16_t saturation, uint16_t value, pos_view_t *view);
/* Three color flashes, staggered flag slides, text fade, then a one-second
 * hold. False after the last frame. Frames require no SD assets. */
bool pos_boot_frame(unsigned step, pos_view_t *view, uint32_t *hold_ms);

typedef struct {
  const char *name;
  size_t state_size;
  bool (*available)(void *services);
  int (*initialize)(void *state, void *services);
  void (*foreground)(void *state);
  void (*event)(void *state, const pos_event_t *event);
  void (*update)(void *state, uint64_t milliseconds);
  void (*render)(void *state, pos_view_t *view);
  int (*save)(void *state);
  void (*exit)(void *state);
} pos_app_t;
typedef struct {
  const pos_app_t *app;
  void *state;
  void *services;
} pos_core_t;
int pos_core_open(pos_core_t *core, const pos_app_t *app);
int pos_core_close(pos_core_t *core);
/* HOME is consumed before the application's handler can see it. */
int pos_core_event(pos_core_t *core, const pos_event_t *event);
void pos_core_tick(pos_core_t *core, uint64_t milliseconds, pos_view_t *view);
