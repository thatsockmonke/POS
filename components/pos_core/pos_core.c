#include "pos_core.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only the foreground consumer owns this latch; the input task owns its queue.
 */
static bool home_request;
void pos_home_request(void) { home_request = true; }
bool pos_home_pending(void) { return home_request; }
bool pos_home_take(void) {
  bool value = home_request;
  home_request = false;
  return value;
}

/* Physical remap and Fn behavior from stock M5Cardputer-UserDemo's
 * main/hal/keyboard/keyboard.cpp at b549eac0a3c65bc108186c276b8fac0a214aaa4e.
 * Fn+letters type uppercase; keys without an override keep working. POS makes
 * the Esc legend primary: bare Esc and Fn+Esc cancel, Shift+Esc types '~',
 * and Alt+Esc types '`'. All app and text contexts receive the same Escape. */
static const int normal[4][14] = {
    {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
     POS_KEY_BACKSPACE},
    {'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
    {0, 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
     POS_KEY_ENTER},
    {0, 0, 0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '}};
static const char shifted[4][15] = {"~!@#$%^&*()_+\0", "\tQWERTYUIOP{}|",
                                    "\0\0ASDFGHJKL:\"\0", "\0\0\0ZXCVBNM<>? "};

bool pos_keyboard_decode(pos_keyboard_t *k, uint8_t raw, pos_event_t *event) {
  unsigned value = raw & 0x7f;
  if (!value)
    return false;
  unsigned row = (value - 1) / 10, col = (value - 1) % 10;
  if (row >= 7 || col >= 8)
    return false;
  unsigned x = row * 2 + (col > 3), y = (col + 4) % 4, index = y * 14 + x;
  bool down = (raw & 0x80) != 0;
  if (k->held[index] == down)
    return false;
  k->held[index] = down;
  unsigned mods =
      (k->held[29] ? POS_MOD_SHIFT : 0) | (k->held[42] ? POS_MOD_CTRL : 0) |
      (k->held[28] ? POS_MOD_FN : 0) | (k->held[44] ? POS_MOD_ALT : 0) |
      (k->held[43] ? POS_MOD_META : 0);
  bool home = k->held[28] &&
              (k->held[55] || k->held[40]); /* Fn+Space, reserved globally */
  if (home && !k->home_held) {
    k->home_held = true;
    *event = (pos_event_t){POS_KEY_HOME, mods, true};
    return true;
  }
  k->home_held = home;
  if (!down) {
    int key = k->pressed_key[index];
    k->pressed_key[index] = 0;
    if (!key)
      return false;
    if (!index && key < 128)
      mods &= ~POS_MOD_ALT;
    *event = (pos_event_t){key, mods, false};
    return true;
  }
  int key = normal[y][x];
  if (!key || (home && (index == 55 || index == 40)))
    return false;
  if (index == 0 && !(mods & POS_MOD_FN))
    key = mods & POS_MOD_SHIFT ? '~'
          : mods & POS_MOD_ALT ? '`'
                              : POS_KEY_ESCAPE;
  else if (mods & POS_MOD_FN) {
    if (index == 0)
      key = POS_KEY_ESCAPE;
    else if (index == 13)
      key = POS_KEY_DELETE;
    else if (index == 39)
      key = POS_KEY_UP;
    else if (index == 52)
      key = POS_KEY_LEFT;
    else if (index == 53)
      key = POS_KEY_DOWN;
    else if (index == 54)
      key = POS_KEY_RIGHT;
    else if (key >= 'a' && key <= 'z') {
      key -= 'a' - 'A';
      mods |= POS_MOD_SHIFT;
    } else if ((mods & POS_MOD_SHIFT) && key < 128)
      key = shifted[y][x];
  } else if ((mods & POS_MOD_SHIFT) && key < 128)
    key = shifted[y][x];
  /* Alt chooses a literal on Esc rather than an application Alt shortcut.
   * Consume it on both edges so editor typing/batching accepts the character. */
  if (!index && key < 128)
    mods &= ~POS_MOD_ALT;
  k->pressed_key[index] = key;
  *event = (pos_event_t){key, mods, down};
  return true;
}

int pos_navigation(const pos_event_t *event) {
  if (!event || !event->pressed)
    return 0;
  /* Interpret arrow legends only in navigation contexts. Text consumers
   * receive the unchanged stock punctuation; Shift still allows fine moves. */
  if (!(event->modifiers & (POS_MOD_CTRL | POS_MOD_ALT | POS_MOD_META))) {
    switch (event->key) {
    case ';':
    case ':':
      return POS_KEY_UP;
    case ',':
    case '<':
      return POS_KEY_LEFT;
    case '.':
    case '>':
      return POS_KEY_DOWN;
    case '/':
    case '?':
      return POS_KEY_RIGHT;
    }
  }
  return event->key;
}

void pos_view_clear(pos_view_t *view, const char *title) {
  memset(view, 0, sizeof(*view));
  view->selected_row = -1;
  pos_view_line(view, 0, "%s", title);
}

void pos_view_line(pos_view_t *view, int row, const char *format, ...) {
  if (row < 0 || row >= POS_ROWS)
    return;
  va_list args;
  va_start(args, format);
  vsnprintf(view->lines[row], sizeof(view->lines[row]), format, args);
  va_end(args);
}

int pos_core_close(pos_core_t *core) {
  int result = 0;
  if (core->app) {
    if (core->app->save)
      result = core->app->save(core->state);
    if (core->app->exit)
      core->app->exit(core->state);
    free(core->state);
  }
  core->app = NULL;
  core->state = NULL;
  return result;
}

int pos_core_open(pos_core_t *core, const pos_app_t *app) {
  if (!app || !app->render ||
      (app->available && !app->available(core->services)) ||
      pos_core_close(core))
    return -1;
  void *state = calloc(1, app->state_size ? app->state_size : 1);
  if (!state)
    return -1;
  if (app->initialize && app->initialize(state, core->services)) {
    if (app->exit)
      app->exit(state);
    free(state);
    return -1;
  }
  core->app = app;
  core->state = state;
  if (app->foreground)
    app->foreground(state);
  return 0;
}

int pos_core_event(pos_core_t *core, const pos_event_t *event) {
  if (event->pressed &&
      (event->key == POS_KEY_HOME || event->key == POS_KEY_ESCAPE)) {
    return pos_core_close(core);
  }
  if (core->app && core->app->event)
    core->app->event(core->state, event);
  return 0;
}

void pos_core_tick(pos_core_t *core, uint64_t milliseconds, pos_view_t *view) {
  if (!core->app)
    return;
  pos_view_clear(view, core->app->name);
  if (core->app->update)
    core->app->update(core->state, milliseconds);
  core->app->render(core->state, view);
}
