/* Reuse the simulated flash hooks, while exercising the real portable services.
 */
#define main pos_installer_runner_main
#include "host_runner.c"
#undef main
#include "cJSON.h"
#include "pos_console.h"
#include "pos_core.h"
#include "pos_device.h"
#include "pos_fs.h"
#include "pos_launcher.h"
#include "pos_recovery.h"
#include "pos_settings.h"
#include "ui.h"
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>

static pos_event_t ui_events[512];
static unsigned ui_next, ui_count, ui_frames;
static bool secret_leaked, notice_tail_seen;
static pos_view_t ui_last;
static bool display_error, device_error, empty_folder_seen, last_page_seen,
    keep_sd_seen;
static unsigned folder_pages_seen, picker_frames;
static bool background_preview_seen;
static pos_view_t picker_last;
static void push_key(int key, unsigned mods) {
  assert(ui_count < sizeof(ui_events) / sizeof(ui_events[0]));
  ui_events[ui_count++] = (pos_event_t){key, mods, true};
}
static void push_text(const char *text) {
  for (; *text; ++text)
    push_key(*text, 0);
}
static void replace_field(const char *text) {
  push_key('u', POS_MOD_CTRL);
  push_text(text);
  push_key(POS_KEY_ENTER, 0);
}
int64_t esp_timer_get_time(void) {
  static int64_t time;
  return time += 200000;
}
bool pos_device_event(pos_event_t *event, uint32_t timeout) {
  if (ui_next >= ui_count) {
    assert(!timeout);
    return false;
  }
  *event = ui_events[ui_next++];
  if (event->pressed && event->key == POS_KEY_HOME)
    pos_home_request();
  return true;
}
void pos_device_show(const pos_view_t *view) {
  ++ui_frames;
  ui_last = *view;
  if (view->color_picker) {
    ++picker_frames;
    picker_last = *view;
  }
  if (view->tiles && strstr(view->lines[14], "PREVIEW"))
    background_preview_seen = true;
  if (!strcmp(view->lines[0], "Display name not valid"))
    display_error = true;
  if (!strcmp(view->lines[0], "Device name not valid"))
    device_error = true;
  unsigned page = 0;
  if (sscanf(view->lines[1], "Folder page %u/", &page) == 1 && page <= 3 &&
      page)
    folder_pages_seen |= 1u << (page - 1);
  for (int row = 0; row < POS_ROWS; ++row) {
    if (strstr(view->lines[row], "Empty folder"))
      empty_folder_seen = true;
    if (strstr(view->lines[row], "Next page (last)"))
      last_page_seen = true;
    if (strstr(view->lines[row], "Keep the SD card inserted."))
      keep_sd_seen = true;
  }
  for (int row = 0; row < POS_ROWS; ++row)
    if (strstr(view->lines[row], "s3cret"))
      secret_leaked = true;
    else if (strstr(view->lines[row], "FINAL_LINE"))
      notice_tail_seen = true;
}
static void queue_text(const char *text) {
  ui_next = ui_count = 0;
  for (; *text; ++text)
    ui_events[ui_count++] = (pos_event_t){*text, 0, true};
  ui_events[ui_count++] = (pos_event_t){POS_KEY_ENTER, 0, true};
}
static int ui_tests(void) {
  char text[32] = "";
  queue_text("wasd");
  assert(pos_ui_text("Text", "Normal letters", text, sizeof(text), false));
  assert(!strcmp(text, "wasd"));
  queue_text("s3cret");
  text[0] = 0;
  assert(pos_ui_text("Wi-Fi", "Password", text, sizeof(text), true));
  assert(!secret_leaked);
  ui_next = 0;
  ui_count = 2;
  ui_events[0] = (pos_event_t){POS_KEY_HOME, 0, true};
  ui_events[1] = (pos_event_t){POS_KEY_ENTER, 0, true};
  assert(!pos_ui_text("Text", "cancel", text, sizeof(text), false));
  unsigned before = ui_frames;
  const char *items[] = {"first", "second"};
  assert(pos_ui_menu("Nested menu", items, 2) == -1);
  pos_ui_notice("Nested notice", "must not block");
  assert(ui_frames == before && ui_next == 1);
  assert(pos_ui_take_home());
  assert(pos_ui_menu("Launcher", items, 2) == 0);
  ui_count = 1;
  ui_next = 0;
  ui_events[0] = (pos_event_t){POS_KEY_HOME, 0, true};
  assert(pos_ui_progress(NULL, "backup", "file", 8, 16) == 1);
  assert(pos_ui_take_home());
  ui_next = 0;
  ui_count = 3;
  for (unsigned i = 0; i < 3; ++i)
    ui_events[i] = (pos_event_t){POS_KEY_ENTER, 0, true};
  char body[128] = "";
  for (unsigned i = 0; i < 30; ++i)
    strcat(body, "a\n");
  strcat(body, "FINAL_LINE");
  pos_ui_notice("Long message", body);
  assert(notice_tail_seen && ui_next == 3);
  const char *long_menu[12];
  for (unsigned i = 0; i < 12; ++i)
    long_menu[i] = "Item";
  queue_text("sssssssssss");
  assert(pos_ui_menu("Paged menu", long_menu, 12) == 11);
  puts("{\"home_bubbles_to_launcher\":true,\"password_display_leaked\":false}");
  return 0;
}

static int boot_splash_tests(void) {
  pos_view_t view;
  uint32_t hold = 0, total = 0;
  for (unsigned step = 0; step < POS_BOOT_COLOR_COUNT; ++step) {
    assert(pos_boot_frame(step, &view, &hold));
    assert(hold == 180 && view.solid_color && !view.boot_splash);
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x)
        assert(pos_view_pixel(&view, x, y) == pos_boot_colors[step]);
    total += hold;
  }
  unsigned slides = POS_BOOT_SLIDE_FRAMES + POS_BOOT_STAGGER_FRAMES;
  for (unsigned frame = 1; frame <= slides; ++frame) {
    assert(pos_boot_frame(POS_BOOT_COLOR_COUNT + frame - 1, &view, &hold));
    assert(hold == 40 && view.boot_splash && !view.solid_color);
    assert(!view.boot_text_alpha);
    unsigned yellow = frame < 10 ? frame * 24 : 240;
    unsigned pink = frame <= 5 ? 0 : frame < 15 ? (frame - 5) * 24 : 240;
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x) {
        unsigned color = y < 45 && x < (int)pink                ? 0
                         : y >= 45 && y < 90 && x < (int)yellow ? 1
                                                                : 2;
        assert(pos_view_pixel(&view, x, y) == pos_boot_colors[color]);
      }
    total += hold;
  }
  uint16_t previous[3] = {pos_boot_colors[0], pos_boot_colors[1],
                          pos_boot_colors[2]};
  for (unsigned frame = 0; frame < POS_BOOT_FADE_FRAMES; ++frame) {
    assert(pos_boot_frame(POS_BOOT_COLOR_COUNT + slides + frame, &view, &hold));
    assert(hold == 40 && view.boot_pink_width == 240 &&
           view.boot_yellow_width == 240);
    for (unsigned stripe = 0; stripe < 3; ++stripe) {
      uint16_t pixel = pos_view_pixel(&view, 30, 22 + stripe * 39);
      assert((pixel >> 11) <= (previous[stripe] >> 11));
      assert(((pixel >> 5) & 63) <= ((previous[stripe] >> 5) & 63));
      assert((pixel & 31) <= (previous[stripe] & 31));
      if (frame == 0)
        assert(pixel != 0 && pixel != pos_boot_colors[stripe]);
      previous[stripe] = pixel;
      assert(pos_view_pixel(&view, 239, stripe * 45) ==
             pos_boot_colors[stripe]);
    }
    total += hold;
  }
  assert(!previous[0] && !previous[1] && !previous[2]);
  assert(pos_boot_frame(POS_BOOT_HOLD_FRAME, &view, &hold));
  assert(hold == 1000 && view.boot_text_alpha == 255);
  total += hold;
  unsigned ink[3] = {0}, ink_stripes[3] = {0};
  int left = 240, right = 0, top = 135, bottom = 0;
  for (int y = 0; y < 135; ++y)
    for (int x = 0; x < 240; ++x) {
      uint16_t pixel = pos_view_pixel(&view, x, y);
      if (pixel == 0) {
        assert(y >= 22 && y < 113 && x >= 21 && x < 219);
        ++ink[(x - 21) / 72];
        ++ink_stripes[y / 45];
        if (x < left)
          left = x;
        if (x > right)
          right = x;
        if (y < top)
          top = y;
        if (y > bottom)
          bottom = y;
      } else
        assert(pixel == pos_boot_colors[y / 45]);
    }
  for (unsigned i = 0; i < 3; ++i)
    assert(ink[i] > 500 && ink_stripes[i] > 500);
  assert(left == 21 && right == 218 && top == 22 && bottom == 112);
  assert(total == 2540);
  assert(!pos_boot_frame(POS_BOOT_HOLD_FRAME + 1, &view, &hold));
  assert(!pos_boot_frame(UINT32_MAX, &view, &hold));
  assert(!pos_boot_frame(0, NULL, &hold));
  assert(!pos_boot_frame(0, &view, NULL));
  pos_view_clear(&view, "Home");
  assert(!view.boot_splash && !view.solid_color && !view.boot_text_alpha);
  assert(!view.boot_pink_width && !view.boot_yellow_width);
  assert(pos_view_pixel(&view, 0, 90) != pos_boot_colors[2]);
  puts("{\"yellow_first\":true,\"pink_staggered\":true,\"smooth_fade\":true,"
       "\"large_centered_letters\":true,\"splash_ms\":1000}");
  return 0;
}
static int text_qol_tests(void) {
  char text[32] = "old contents";
  pos_keyboard_t keyboard = {0};
  pos_event_t event;
  assert(!pos_keyboard_decode(&keyboard, 0x84, &event)); /* physical Ctrl */
  assert(pos_keyboard_decode(&keyboard, 0xa4, &event) && event.key == 'u' &&
         (event.modifiers & POS_MOD_CTRL));
  ui_next = ui_count = 0;
  ui_events[ui_count++] = event;
  push_text("new");
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_text("Text", "Replace", text, sizeof(text), false));
  assert(!strcmp(text, "new") && !strcmp(ui_last.lines[10], "3/31 characters"));
  ui_next = ui_count = 0;
  push_key(21, 0);
  push_text("s3cret");
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_text("Password", "Masked", text, sizeof(text), true));
  assert(!secret_leaked && !strcmp(text, "s3cret"));
  ui_next = ui_count = 0;
  push_key('U', POS_MOD_CTRL | POS_MOD_SHIFT);
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_text("Text", "Clear", text, sizeof(text), false));
  for (unsigned i = 0; i < sizeof(text); ++i)
    assert(!text[i]);
  char short_text[4] = "";
  queue_text("abcdef");
  assert(pos_ui_text("Text", "Limit", short_text, sizeof(short_text), false));
  assert(!strcmp(short_text, "abc") &&
         !strcmp(ui_last.lines[10], "3/3 characters"));
  puts("{\"ctrl_u\":true,\"usb_ctrl_u\":true,\"password_masked\":true,\"limit_"
       "enforced\":true}");
  return 0;
}
static int setup_tests(const char *root, const char *mode) {
  pos_settings_t current;
  pos_settings_defaults(&current);
  int loaded = pos_settings_load(root, &current);
  assert(loaded == 0 || loaded == 1);
  pos_settings_t before = current;
  bool retry = strstr(mode, "retry") != NULL;
  ui_next = ui_count = 0;
  if (retry) {
    replace_field("");
    push_key(POS_KEY_ENTER, 0);
  }
  replace_field("Stevie");
  if (retry) {
    replace_field("PAN PUTER");
    push_key(POS_KEY_ENTER, 0);
  }
  replace_field("PANPUTER");
  if (strstr(mode, "cancel-time"))
    push_key(POS_KEY_ESCAPE, 0);
  else {
    push_key('s', 0);
    push_key(POS_KEY_ENTER, 0); /* Pacific */
    if (strstr(mode, "home"))
      push_key(POS_KEY_HOME, 0);
    else if (strstr(mode, "cancel"))
      push_key(POS_KEY_ESCAPE, 0);
    else {
      push_key('s', 0);
      push_key(POS_KEY_ENTER, 0);
    }
  }
  if (strstr(mode, "fail"))
    push_key(POS_KEY_ENTER, 0); /* save-error notice */
  bool saved = pos_ui_setup(root, &current, loaded == 1);
  assert(ui_next == ui_count);
  if (!saved)
    assert(!memcmp(&current, &before, sizeof(current)));
  if (saved)
    assert(!strcmp(current.display_name, "Stevie") &&
           !strcmp(current.device_name, "PANPUTER") &&
           current.brightness == 70 &&
           !strcmp(current.timezone, "PST8PDT,M3.2.0,M11.1.0"));
  printf("{\"saved\":%s,\"display_error\":%s,\"device_error\":%s}\n",
         saved ? "true" : "false", display_error ? "true" : "false",
         device_error ? "true" : "false");
  return 0;
}
static void choose(unsigned index) {
  for (unsigned i = 0; i < index; ++i)
    push_key('s', 0);
  push_key(POS_KEY_ENTER, 0);
}
static int browser_tests(const char *root, const char *mode) {
  ui_next = ui_count = 0;
  if (!strcmp(mode, "browse-pages")) {
    choose(2);
    choose(2);
    choose(1);
    choose(1);
  }
  if (!strcmp(mode, "browse-empty")) {
    choose(2);
    choose(1);
  }
  if (!strcmp(mode, "browse-preview")) {
    choose(2);
    choose(3);
    push_key(POS_KEY_ENTER, 0);
  }
  push_key(POS_KEY_ESCAPE, 0);
  pos_ui_browse(root, "");
  assert(ui_next == ui_count);
  printf("{\"pages\":%u,\"empty\":%s,\"last_page\":%s,\"final_page\":\"%s\"}\n",
         folder_pages_seen, empty_folder_seen ? "true" : "false",
         last_page_seen ? "true" : "false", ui_last.lines[1]);
  return 0;
}
static int phase_tests(void) {
  const char *phases[] = {"copy", "verify", "firmware_write",
                          "firmware_verify"};
  const char *titles[] = {"Prepare SD system", "Verify files",
                          "Write runtime firmware", "Verify runtime firmware"};
  ui_next = ui_count = 0;
  for (unsigned i = 0; i < 4; ++i) {
    assert(!pos_ui_progress(NULL, phases[i], "file", 1, 2));
    assert(!strcmp(ui_last.lines[0], titles[i]));
  }
  push_key(POS_KEY_ENTER, 0);
  pos_ui_install_complete();
  assert(keep_sd_seen && ui_next == ui_count);
  puts("{\"phases_clear\":true,\"keep_sd_inserted\":true}");
  return 0;
}
static int background_tests(const char *root, const char *mode) {
  pos_settings_t settings;
  pos_settings_defaults(&settings);
  (void)pos_settings_load(root, &settings);
  pos_settings_t before = settings;
  ui_next = ui_count = 0;
  push_key(POS_KEY_ENTER, 0);
  replace_field(!strcmp(mode, "background-invalid-count") ? "0" : "4");
  if (!strcmp(mode, "background-invalid-count")) {
    push_key(POS_KEY_ENTER, 0); /* dismiss invalid count */
    push_key(POS_KEY_ESCAPE, 0);
  } else {
    push_text("s");
    push_key(POS_KEY_ENTER, 0); /* first stripe */
    push_text("dse");
    push_key('W', POS_MOD_SHIFT);
    if (!strcmp(mode, "background-home")) {
      push_key(POS_KEY_HOME, 0);
    } else {
      push_key(POS_KEY_ENTER, 0); /* accept color */
      if (!strcmp(mode, "background-cancel")) {
        push_key(POS_KEY_ESCAPE, 0);
      } else {
        push_text("ssss");
        push_key(POS_KEY_ENTER, 0); /* preview at index 5 */
        push_key(POS_KEY_ENTER, 0); /* return to editor */
        push_text("s");
        push_key(POS_KEY_ENTER, 0); /* Save & Apply */
        if (!strcmp(mode, "background-save-failure")) {
          push_key(POS_KEY_ENTER, 0); /* save error */
          push_key(POS_KEY_ESCAPE, 0);
        }
      }
    }
  }
  bool saved_background = pos_ui_background(root, &settings);
  bool success = !strcmp(mode, "background-save");
  assert(saved_background == success);
  if (success) {
    assert(background_preview_seen && picker_frames > 1);
    assert(settings.background.count == 4);
    assert(settings.background.colors[0] != before.background.colors[0]);
    assert(settings.background.colors[3] == 0xffffff);
    pos_settings_t reloaded;
    assert(!pos_settings_load(root, &reloaded));
    assert(!memcmp(&settings.background, &reloaded.background,
                   sizeof(settings.background)));
    assert(!strcmp(settings.display_name, before.display_name));
  } else {
    assert(!memcmp(&settings, &before, sizeof(settings)));
  }
  if (!strcmp(mode, "background-home"))
    assert(pos_ui_take_home());
  assert(ui_next == ui_count);
  puts("{\"draft_save_cancel_verified\":true}");
  return 0;
}
static int picker_tests(void) {
  uint32_t rgb = 0x808080, original = rgb;
  ui_next = ui_count = 0;
  push_text("dse");
  push_key('W', POS_MOD_SHIFT);
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Pick a color", &rgb));
  assert(picker_last.picker_hue == 10 && picker_last.picker_saturation == 25 &&
         picker_last.picker_value == 481 && rgb != original);
  assert(picker_frames == 5);
  original = rgb;
  ui_next = ui_count = 0;
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Keep exact color", &rgb) && rgb == original);
  ui_next = ui_count = 0;
  push_text("aaa");
  push_key(POS_KEY_ESCAPE, 0);
  assert(!pos_ui_color("Cancel color", &rgb) && rgb == original);
  ui_next = ui_count = 0;
  push_key(POS_KEY_HOME, 0);
  assert(!pos_ui_color("Close app", &rgb) && rgb == original &&
         pos_ui_take_home());
  rgb = 0xffffff;
  ui_next = ui_count = 0;
  push_text("aw"); /* clamp saturation and brightness at the edges */
  push_key('d', POS_MOD_CTRL);
  ui_events[ui_count++] = (pos_event_t){'s', 0, false};
  push_key(POS_KEY_ENTER, 0);
  assert(pos_ui_color("Bounds", &rgb) && rgb == 0xffffff);
  pos_view_t color_view;
  pos_color_picker_view("Color", 0xabcdef, 0, 500, 500, &color_view);
  assert(pos_view_pixel(&color_view, 8, 28) == 0xffff);
  assert(pos_view_pixel(&color_view, 167, 28) == 0xf800);
  assert(pos_view_pixel(&color_view, 8, 99) == 0);
  assert(pos_view_pixel(&color_view, 90, 64) == 0xffff); /* cursor ring */
  assert(pos_view_pixel(&color_view, 91, 64) == 0);      /* cursor contrast */
  assert(pos_view_pixel(&color_view, 200, 40) == pos_color_rgb565(0xabcdef));
  assert(pos_color_hsv_rgb(0, 1000, 1000) == 0xff0000);
  assert(pos_color_hsv_rgb(60, 1000, 1000) == 0xffff00);
  assert(pos_color_hsv_rgb(120, 1000, 1000) == 0x00ff00);
  assert(pos_color_hsv_rgb(180, 1000, 1000) == 0x00ffff);
  assert(pos_color_hsv_rgb(240, 1000, 1000) == 0x0000ff);
  assert(pos_color_hsv_rgb(300, 1000, 1000) == 0xff00ff);
  assert(pos_color_parse("#aB12ef", &rgb) && rgb == 0xab12ef);
  assert(!pos_color_parse("#GG0000", &rgb) &&
         !pos_color_parse("FFFFFFF", &rgb));
  puts("{\"wasd_picker\":true,\"fine_adjustment\":true,\"cancel_preserves_"
       "color\":true}");
  return 0;
}
static int launcher_tests(const char *root) {
  assert(POS_LAUNCHER_APP_COUNT == 7);
  pos_event_t event = {'w', 0, true};
  assert(pos_launcher_action(&event) == POS_KEY_ENTER);
  event.key = 'a';
  assert(pos_launcher_action(&event) == POS_KEY_LEFT);
  event.key = 'd';
  assert(pos_launcher_action(&event) == POS_KEY_RIGHT);
  event.key = 's';
  assert(!pos_launcher_action(&event));
  event.key = 'w';
  event.pressed = false;
  assert(!pos_launcher_action(&event));
  event.pressed = true;
  event.modifiers = POS_MOD_CTRL;
  assert(!pos_launcher_action(&event));
  pos_view_t view;
  pos_background_t background;
  pos_background_defaults(&background);
  for (unsigned count = 1; count <= 12; ++count) {
    assert(pos_background_resize(&background, count));
    for (unsigned i = 0; i < count; ++i)
      background.colors[i] = (i + 1) * 0x112233;
    for (int app = 0; app < POS_LAUNCHER_APP_COUNT; ++app) {
      pos_launcher_view(root, "PANPUTER", &background, app, &view);
      assert(view.selected_tile == 1 &&
             strstr(view.lines[12], pos_launcher_names[app]));
      for (int y = 8; y < 112; ++y)
        assert(pos_view_pixel(&view, 0, y) ==
               pos_color_rgb565(background.colors[y * count / 135]));
      /* Icons can only contribute black ink or a white outline. */
      unsigned black = 0, white = 0;
      for (int y = 47; y <= 80; ++y)
        for (int x = 23; x <= 56; ++x) {
          uint16_t pixel = pos_view_pixel(&view, x, y);
          if (pixel == 0)
            ++black;
          else if (pixel == 0xffff)
            ++white;
          else
            assert(pixel ==
                   pos_color_rgb565(background.colors[y * count / 135]));
        }
      assert(black && white);
    }
  }
  assert(!pos_background_resize(&background, 0) &&
         !pos_background_resize(&background, 13));
  puts("{\"launcher_controls\":true,\"all_stripe_counts\":true,\"outlined_"
       "black_icons\":true}");
  return 0;
}
static const char *cancel_phase;
static uint64_t cancel_after;
static int recovery_progress(void *opaque, const char *phase, const char *name,
                             uint64_t done, uint64_t total) {
  (void)name;
  (void)total;
  context_t *ctx = opaque;
  if (ctx->interrupt && !strcmp(ctx->interrupt, phase))
    _exit(99);
  return cancel_phase && !strcmp(cancel_phase, phase) && done >= cancel_after;
}

static int initialized, foregrounded, handled, saved, exited, allocations;
static bool fail_save;
typedef struct {
  void *resource;
} test_app_state_t;
static int app_initialize(void *opaque, void *services) {
  test_app_state_t *s = opaque;
  assert(!allocations); /* Prior app must release resources before
                           initialization. */
  s->resource = malloc(128);
  if (!s->resource)
    return -1;
  ++allocations;
  ++initialized;
  return services ? -1 : 0;
}
static void app_foreground(void *opaque) {
  (void)opaque;
  ++foregrounded;
}
static void app_event(void *opaque, const pos_event_t *e) {
  (void)opaque;
  assert(e->key == 'w');
  ++handled;
}
static void app_render(void *opaque, pos_view_t *view) {
  (void)opaque;
  pos_view_line(view, 4, "hello");
}
static int app_save(void *opaque) {
  (void)opaque;
  ++saved;
  return fail_save ? -1 : 0;
}
static void app_exit(void *opaque) {
  test_app_state_t *s = opaque;
  if (s->resource) {
    free(s->resource);
    --allocations;
  }
  ++exited;
}
static int core_tests(void) {
  pos_keyboard_t keyboard = {0};
  pos_event_t event;
  assert(pos_keyboard_decode(&keyboard, 0x8c, &event) && event.key == 'w' &&
         event.pressed);
  assert(pos_navigation(&event) == POS_KEY_UP && event.key == 'w');
  assert(pos_keyboard_decode(&keyboard, 0x0c, &event) && !event.pressed);
  assert(!pos_keyboard_decode(&keyboard, 0x87, &event)); /* shift */
  assert(pos_keyboard_decode(&keyboard, 0x8c, &event) && event.key == 'W');
  assert(pos_keyboard_decode(&keyboard, 0x0c, &event));
  assert(!pos_keyboard_decode(&keyboard, 0x07, &event));
  assert(!pos_keyboard_decode(&keyboard, 0x83, &event)); /* Fn */
  assert(pos_keyboard_decode(&keyboard, 0xc4, &event) &&
         event.key == POS_KEY_HOME);
  assert(!pos_keyboard_decode(&keyboard, 0xc4,
                              &event)); /* duplicate does not repeat Home */
  assert(!pos_keyboard_decode(&keyboard, 0, &event));
  assert(!pos_keyboard_decode(&keyboard, 0xff, &event));
  assert(pos_keyboard_decode(&keyboard, 0x81, &event) &&
         event.key == POS_KEY_ESCAPE);
  const pos_app_t app = {.name = "test",
                         .state_size = sizeof(test_app_state_t),
                         .initialize = app_initialize,
                         .foreground = app_foreground,
                         .event = app_event,
                         .render = app_render,
                         .save = app_save,
                         .exit = app_exit};
  pos_core_t core = {0};
  pos_view_t view;
  for (int i = 0; i < 100; ++i) {
    assert(!pos_core_open(&core, &app));
    assert(core.state);
    event = (pos_event_t){'w', 0, true};
    assert(!pos_core_event(&core, &event));
    pos_core_tick(&core, 1, &view);
    assert(!strcmp(view.lines[4], "hello"));
    event = (pos_event_t){POS_KEY_HOME, 0, true};
    assert(!pos_core_event(&core, &event));
    assert(!core.app && !core.state && !allocations);
  }
  core.services = &core;
  assert(pos_core_open(&core, &app) == -1);
  assert(!allocations && !core.app);
  assert(initialized == 101 && foregrounded == 100 && handled == 100 &&
         saved == 100 && exited == 101);
  core.services = NULL;
  assert(!pos_core_open(&core, &app));
  int init_before = initialized, exits_before = exited, saves_before = saved;
  assert(!pos_core_open(&core, &app));
  assert(initialized == init_before + 1 && exited == exits_before + 1 &&
         saved == saves_before + 1 && allocations == 1);
  core.services =
      &core; /* Failed replacement must clean up both old and new. */
  assert(pos_core_open(&core, &app) == -1);
  assert(!allocations && !core.app && !core.state);
  core.services = NULL;
  assert(!pos_core_open(&core, &app));
  init_before = initialized;
  fail_save = true;
  assert(pos_core_open(&core, &app) == -1);
  assert(initialized == init_before && !allocations && !core.app &&
         !core.state);
  fail_save = false;
  const uint32_t rgb[] = {0xff218c, 0xffd800, 0x21b1ff};
  assert(POS_BOOT_COLOR_COUNT == 3 && POS_BOOT_COLOR_MS == 180);
  for (unsigned i = 0; i < POS_BOOT_COLOR_COUNT; ++i) {
    uint16_t expected = ((rgb[i] >> 19) << 11) | (((rgb[i] >> 10) & 63) << 5) |
                        ((rgb[i] >> 3) & 31);
    assert(pos_boot_colors[i] == expected);
    pos_view_clear(&view, "Text must not cover solid splash");
    view.solid_color = true;
    view.color_rgb565 = pos_boot_colors[i];
    for (int y = 0; y < 135; ++y)
      for (int x = 0; x < 240; ++x)
        assert(pos_view_pixel(&view, x, y) == pos_boot_colors[i]);
  }
  pos_view_clear(&view, "title");
  pos_view_line(&view, -1, "bad");
  pos_view_line(&view, POS_ROWS, "bad");
  pos_view_line(&view, 1, "%0500d", 1);
  assert(strlen(view.lines[1]) == POS_COLS);
  puts("{\"lifecycle_cycles\":100,\"resources_remaining\":0,\"global_home_"
       "intercepted\":true}");
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  if (!strcmp(argv[2], "install") || !strcmp(argv[2], "boot") ||
      !strcmp(argv[2], "validate"))
    return pos_installer_runner_main(argc, argv);
  if (!strcmp(argv[2], "console")) {
    int descriptors[2];
    assert(!pipe(descriptors));
    assert(!pos_console_nonblocking(descriptors[0]));
    assert(pos_console_poll(descriptors[0]) == -1);
    assert(write(descriptors[1], "w", 1) == 1);
    assert(pos_console_poll(descriptors[0]) == 'w');
    assert(pos_console_poll(descriptors[0]) == -1);
    assert(pos_console_nonblocking(-1) == -1);
    close(descriptors[0]);
    close(descriptors[1]);
    puts("{\"nonblocking\":true}");
    return 0;
  }
  if (!strcmp(argv[2], "picker"))
    return picker_tests();
  if (!strcmp(argv[2], "launcher"))
    return launcher_tests(argv[1]);
  if (!strncmp(argv[2], "background-", 11))
    return background_tests(argv[1], argv[2]);
  if (!strcmp(argv[2], "boot-splash"))
    return boot_splash_tests();
  if (!strcmp(argv[2], "text-qol"))
    return text_qol_tests();
  if (!strncmp(argv[2], "setup-", 6))
    return setup_tests(argv[1], argv[2]);
  if (!strncmp(argv[2], "browse-", 7))
    return browser_tests(argv[1], argv[2]);
  if (!strcmp(argv[2], "phase-labels"))
    return phase_tests();
  if (!strcmp(argv[2], "core"))
    return core_tests();
  if (!strcmp(argv[2], "ui"))
    return ui_tests();
  context_t ctx = {.root = argv[1], .available = 1024ULL * 1024 * 1024};
  const char *archive = NULL, *name = NULL;
  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "--free") && i + 1 < argc)
      ctx.available = strtoull(argv[++i], NULL, 10);
    else if (!strcmp(argv[i], "--archive") && i + 1 < argc)
      archive = argv[++i];
    else if (!strcmp(argv[i], "--name") && i + 1 < argc)
      name = argv[++i];
    else if (!strcmp(argv[i], "--interrupt") && i + 1 < argc)
      ctx.interrupt = argv[++i];
    else if (!strcmp(argv[i], "--cancel") && i + 1 < argc)
      cancel_phase = argv[++i];
    else if (!strcmp(argv[i], "--file-limit") && i + 1 < argc) {
      rlim_t limit = (rlim_t)strtoull(argv[++i], NULL, 10);
      struct rlimit limits = {limit, limit};
      assert(!setrlimit(RLIMIT_FSIZE, &limits));
      signal(SIGXFSZ, SIG_IGN);
    } else if (!strcmp(argv[i], "--after") && i + 1 < argc)
      cancel_after = strtoull(argv[++i], NULL, 10);
    else
      return 2;
  }
  char backup[POS_FS_PATH];
  pos_fs_join(backup, ctx.root, POS_BACKUP_NAME);
  if (!archive) {
    pos_error_t ignored = {0};
    (void)pos_backup_find(ctx.root, backup, &ignored);
    archive = backup;
  }
  pos_error_t error = {0};
  pos_backup_info_t info = {0};
  int result = -1;
  if (!strcmp(argv[2], "scan"))
    result = pos_backup_scan(ctx.root, &info, recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "backup"))
    result = pos_backup_create(ctx.root, "0.2.0-dev", ctx.available, 0,
                               recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "resume-backup"))
    result = pos_backup_set_resume(ctx.root, "0.2.0-dev", ctx.available, 0,
                                   recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "backup-set"))
    result = pos_backup_set_create(ctx.root, "0.2.0-dev", ctx.available, 0,
                                   recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "verify"))
    result = pos_backup_verify(archive, &info, recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "restore"))
    result = pos_backup_restore(ctx.root, archive, ctx.available, &info,
                                recovery_progress, &ctx, &error);
  else if (!strcmp(argv[2], "match"))
    result = pos_backup_matches(ctx.root, archive, &error);
  else if (!strcmp(argv[2], "layout"))
    result = pos_fs_layout(ctx.root);
  else if (!strncmp(argv[2], "settings-", 9)) {
    pos_settings_t settings;
    pos_settings_defaults(&settings);
    int loaded = pos_settings_load(ctx.root, &settings);
    if (!strcmp(argv[2], "settings-read"))
      result = loaded;
    else {
      if (name) {
        if (strlen(name) >= sizeof(settings.display_name))
          return 1;
        strcpy(settings.display_name, name);
      }
      settings.setup_complete = true;
      result = pos_settings_save(ctx.root, &settings);
    }
    if (!result) {
      cJSON *json = cJSON_CreateObject();
      cJSON_AddStringToObject(json, "name", settings.display_name);
      cJSON_AddNumberToObject(json, "generation", settings.generation);
      cJSON_AddNumberToObject(json, "brightness", settings.brightness);
      cJSON *colors = cJSON_AddArrayToObject(json, "background");
      for (unsigned i = 0; i < settings.background.count; ++i)
        cJSON_AddItemToArray(colors,
                             cJSON_CreateNumber(settings.background.colors[i]));
      char *text = cJSON_PrintUnformatted(json);
      puts(text);
      free(text);
      cJSON_Delete(json);
    }
  } else if (!strcmp(argv[2], "repair") || !strcmp(argv[2], "reinstall")) {
    char path[POS_FS_PATH];
    pos_fs_join(path, ctx.root, "pos-install/manifest.pos");
    pos_package_t *p = NULL;
    result = pos_load_manifest(path, &p, &error);
    pos_platform_t platform = {.ctx = &ctx,
                               .free_bytes = free_space,
                               .check_firmware = check_image,
                               .write_firmware = write_image,
                               .select_firmware = select_image,
                               .report = progress};
    if (!result) {
      if (!strcmp(argv[2], "repair"))
        result = pos_repair_system(ctx.root, p, &platform, &error);
      else {
        result = pos_reinstall_prepare(ctx.root, p, &platform, &error);
        if (!result && ctx.interrupt && !strcmp(ctx.interrupt, "retired"))
          _exit(99);
        if (!result)
          result = pos_install(ctx.root, p, &platform, &error);
      }
    }
    pos_package_free(p);
  }
  if (result) {
    fprintf(stderr, "%s\n",
            *error.message ? error.message : "Operation failed safely");
    return 1;
  }
  if (!strcmp(argv[2], "scan") || !strcmp(argv[2], "verify") ||
      !strcmp(argv[2], "restore"))
    printf("{\"files\":%u,\"bytes\":%llu,\"required\":%llu,\"conflicts\":%u}\n",
           info.files, (unsigned long long)info.bytes,
           (unsigned long long)info.required, info.conflicts);
  return 0;
}
