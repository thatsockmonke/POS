#include "pos_launcher.h"
#include "pos_fs.h"
#include <stdio.h>
#include <string.h>

const char *const pos_launcher_names[POS_LAUNCHER_APP_COUNT] = {
    "Counter", "Settings",   "Diagnostics", "Files",      "Recovery",
    "About",   "Background", "Text Editor", "Wi-Fi",      "Music",
    "Share",   "Location",   "Radio",       "Range Test", "Recorder"};

int pos_launcher_action(const pos_event_t *event) {
  if (!event || !event->pressed ||
      (event->modifiers & (POS_MOD_CTRL | POS_MOD_ALT)))
    return 0;
  switch (pos_navigation(event)) {
  case POS_KEY_LEFT:
    return POS_KEY_LEFT;
  case POS_KEY_RIGHT:
    return POS_KEY_RIGHT;
  case POS_KEY_UP:
  case POS_KEY_ENTER:
    return POS_KEY_ENTER;
  default:
    return 0;
  }
}
void pos_launcher_view(const char *root, const char *device,
                       const pos_background_t *background, int selected,
                       pos_view_t *view) {
  if (selected < 0 || selected >= POS_LAUNCHER_APP_COUNT)
    selected = 0;
  pos_view_clear(view, device);
  view->tiles = true;
  view->selected_tile = 1;
  if (pos_background_valid(background))
    view->background = *background;
  else
    pos_background_defaults(&view->background);
  static const uint8_t fallback[8] = {0x3c, 0x42, 0x5a, 0x52,
                                      0x4a, 0x42, 0x3c, 0};
  char path[POS_FS_PATH];
  FILE *icons = root && !pos_fs_join(path, root, "System/Assets/apps.icons")
                    ? fopen(path, "rb")
                    : NULL;
  for (unsigned tile = 0; tile < 7; ++tile) {
    int app = (selected + POS_LAUNCHER_APP_COUNT + (int)tile - 3) %
              POS_LAUNCHER_APP_COUNT;
    if (!icons || fseek(icons, app * 8, SEEK_SET) ||
        fread(view->icons[tile], 1, 8, icons) != 8)
      memcpy(view->icons[tile], fallback, sizeof(fallback));
  }
  if (icons)
    fclose(icons);
  const char *name = pos_launcher_names[selected];
  pos_view_line(view, 12, "%*s%s", (int)(POS_COLS - strlen(name)) / 2, "",
                name);
  pos_view_line(view, 14, "App %d/%d", selected + 1, POS_LAUNCHER_APP_COUNT);
  pos_view_line(view, 15, "Left/Right:move Up:open");
}
