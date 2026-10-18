/* Native companion for exercising the exact portable POS services on Linux. */
#include "pos_core.h"
#include "pos_fs.h"
#include "pos_launcher.h"
#include "pos_maps.h"
#include "pos_recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

static int progress(void *ctx, const char *phase, const char *name,
                    uint64_t done, uint64_t total) {
  (void)ctx;
  static uint64_t last;
  if (done - last >= 1048576 || !done) {
    fprintf(stderr, "%s: %s %llu / %llu\n", phase, name,
            (unsigned long long)done, (unsigned long long)total);
    last = done;
  }
  return 0;
}
static int preview(const char *root, const char *output, unsigned screen,
                   unsigned frame) {
  pos_view_t v;
  static uint8_t map_pixels[POS_MAP_PIXELS], route_pixels[POS_MAP_PIXELS];
  if (screen == 9) {
    char path[POS_FS_PATH];
    pos_error_t e = {0};
    if (pos_fs_join(path, root, "System/Assets/world.map") ||
        pos_world_raster(path, 34.05, -118.248, 0, map_pixels, &e))
      return 1;
    pos_view_clear(&v, "World / North up");
    v.black_background = true;
    v.map_world = true;
    v.map_pixels = map_pixels;
    v.map_fix = true;
    v.map_gps_x = 120;
    v.map_gps_y = 44;
    pos_view_line(&v, 1, "Follow 1x P:30deg");
    pos_view_line(&v, 13, "34.0500 -118.2480");
    pos_view_line(&v, 14, "Arrows:pan +/-:zoom P:step");
    pos_view_line(&v, 15, "C:GPS 0:world Enter:menu");
  } else if (screen == 8) {
    /* Deterministic example roads, rendered with the actual device compositor.
     */
    memset(map_pixels, 0, sizeof(map_pixels));
    for (int y = 0; y < POS_MAP_HEIGHT; ++y)
      for (int x = 0; x < 240; ++x)
        if ((x + y / 2) % 46 < 2 || (y - x / 4 + 240) % 27 < 2) {
          unsigned point = y * 240 + x;
          map_pixels[point / 8] |= 1u << (point % 8);
        }
    pos_view_clear(&v, "Maps / North up");
    v.black_background = true;
    memset(route_pixels, 0, sizeof(route_pixels));
    pos_map_line(route_pixels, 94, 85, 94, 44);
    pos_map_line(route_pixels, 94, 44, 180, 44);
    v.map_pixels = map_pixels;
    v.map_route_pixels = route_pixels;
    v.map_region = true;
    v.map_left = 80;
    v.map_right = 154;
    v.map_top = 7;
    v.map_bottom = 81;
    v.map_turn = 3;
    v.map_fix = v.map_waypoint = true;
    v.map_gps_x = 94;
    v.map_gps_y = 68;
    v.map_target_x = 180;
    v.map_target_y = 44;
    pos_view_line(&v, 1, "Turn right in 40m");
    pos_view_line(&v, 13, "Example Road");
    pos_view_line(&v, 14, "286m P:250m +/- Enter:menu");
    pos_view_line(&v, 15, "(c) OpenStreetMap contributors");
  } else if (screen == 1) {
    uint32_t hold;
    if (!pos_boot_frame(frame, &v, &hold))
      return 2;
  } else if (screen == 2) {
    uint16_t hue, saturation, value;
    pos_color_rgb_hsv(0xff218c, &hue, &saturation, &value);
    pos_color_picker_view("Stripe 1 color", 0xff218c, hue, saturation, value,
                          &v);
  } else if (screen >= 3 && screen <= 6) {
    pos_view_clear(&v, screen == 3   ? "Recorder"
                       : screen == 4 ? "Files"
                       : screen == 5 ? "Background"
                                     : "Counter");
    v.black_background = true;
    pos_background_defaults(&v.background);
    if (screen == 3) {
      v.visual = POS_VISUAL_MIC;
      v.visual_level = frame < 10 ? 200 + frame * 65 : 650;
      if (frame >= 100 && frame < 103)
        v.fade = (frame - 99) * 85;
      pos_view_line(&v, 1, "Recording");
      pos_view_line(&v, 2, "Recording-00001.wav");
      pos_view_line(&v, 3, "0:12");
      pos_view_line(&v, 12, "            %u.%u%% Gain32x", v.visual_level / 10,
                    v.visual_level % 10);
      pos_view_line(&v, 13, "L/R: gain    Space: pause");
      pos_view_line(&v, 14, "Enter/Esc: stop & save");
      pos_view_line(&v, 15, "Fn+': close     G0: home");
    } else if (screen == 4) {
      pos_view_line(&v, 1, "Folder page 1/1");
      const char *items[] = {".. (parent)", "Previous (first)",
                             "Next (last)", "Folder actions",
                             "[ ] Documents", "[x] Music",
                             "[ ] Recordings"};
      for (unsigned i = 0; i < 7; ++i)
        pos_view_line(&v, 3 + i, "%s", items[i]);
      v.selected_row = 7;
      pos_view_line(&v, 2, "1 checked | Space Ctrl+C/X/V");
      pos_view_line(&v, 15, "Arrows Enter:OK Fn+`:back");
    } else if (screen == 5) {
      v.visual = POS_VISUAL_STRIPES;
      v.visual_selection = 1;
      v.visual_flash = frame < 2;
      pos_view_line(&v, 1, "Draft - S saves / Esc cancels");
      pos_view_line(&v, 2, "Stripe 2/3  #FFD800");
      pos_view_line(&v, 14, "Up/Down:stripe Enter:color");
      pos_view_line(&v, 15, "L/R:count S:save P:presets");
    } else {
      static const int8_t press[] = {12, 8, 3, 0, 2, 0};
      v.visual = POS_VISUAL_COUNTER;
      v.visual_motion = frame < 6 ? press[frame] : 0;
      pos_view_line(&v, 4, "42");
      pos_view_line(&v, 14, "Enter/Space: +1   Down: -1");
      pos_view_line(&v, 15, "Fn+': close     G0: home");
    }
  } else {
    pos_background_t background;
    pos_background_defaults(&background);
    pos_launcher_view(root, "PANPUTER", &background,
                      screen == 7 ? 14 : POS_LAUNCHER_APP_COUNT - 1, &v);
    if (screen == 7) {
      unsigned f = frame >= 18 ? (frame - 18) % 6 : 0;
      int spring = pos_motion_spring(f);
      if (frame < 18) {
        pos_ribbon_t motion = {0};
        pos_ribbon_nudge(&motion, 1);
        for (unsigned i = 0; i < frame; ++i)
          pos_ribbon_tick(&motion, 32);
        v.ribbon_offset = pos_ribbon_offset(&motion);
      } else if (frame < 24) {
        v.tile_scale = 100 + (100 - spring) * 48 / 100;
        v.fade = f >= 3 ? (f - 2) * 85 : 0;
      } else {
        v.tile_scale = 100 + spring * 48 / 100;
        v.fade = f < 2 ? 180 - f * 90 : 0;
      }
    }
  }
  /* Example UI state, not a measurement from a connected device. */
  v.battery_overlay = true;
  v.battery_available = true;
  v.battery_percent = 75;
  v.battery_bars = 3;
  FILE *f = fopen(output, "wbx");
  if (!f)
    return 1;
  fprintf(f, "P6\n240 135\n255\n");
  int result = 0;
  for (int y = 0; y < 135; ++y)
    for (int x = 0; x < 240; ++x) {
      uint16_t c = pos_view_pixel(&v, x, y);
      unsigned char rgb[] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                             (unsigned char)(((c >> 5) & 63) * 255 / 63),
                             (unsigned char)((c & 31) * 255 / 31)};
      if (fwrite(rgb, 1, 3, f) != 3)
        result = 1;
    }
  if (fclose(f))
    result = 1;
  return result;
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  const char *root = argv[1], *command = argv[2];
  if (!strcmp(command, "boot-frames")) {
    if (argc != 4)
      return 2;
    pos_view_t view;
    uint32_t hold;
    for (unsigned frame = 0; pos_boot_frame(frame, &view, &hold); ++frame) {
      char output[POS_FS_PATH];
      int size = snprintf(output, sizeof(output), "%s/%u.ppm", argv[3], frame);
      if (size < 0 || (size_t)size >= sizeof(output))
        return 2;
      int result = preview(root, output, true, frame);
      if (result)
        return result;
      printf("%u\n", (unsigned)hold);
    }
    return 0;
  }
  if (!strcmp(command, "ui-frames")) {
    if (argc != 4)
      return 2;
    for (unsigned frame = 0; frame < 41; ++frame) {
      unsigned screen = frame < 24 || frame >= 35 ? 7 : 3;
      unsigned pose = frame < 24    ? frame
                      : frame >= 35 ? 24 + frame - 35
                      : frame >= 32 ? 100 + frame - 32
                                    : frame - 24;
      char output[POS_FS_PATH];
      if (snprintf(output, sizeof(output), "%s/%u.ppm", argv[3], frame) >=
          (int)sizeof(output))
        return 2;
      if (preview(root, output, screen, pose))
        return 1;
      printf("%u\n", frame < 18                  ? 32
                     : frame >= 32 && frame < 35 ? 35
                     : frame >= 18 && frame < 24 ? 45
                     : screen == 7               ? 65
                                                 : 120);
    }
    return 0;
  }
  if (!strcmp(command, "world-preview"))
    return argc == 4 ? preview(root, argv[3], 9, 0) : 2;
  if (!strcmp(command, "maps-preview"))
    return argc == 4 ? preview(root, argv[3], 8, 0) : 2;
  const char *visuals[] = {"recorder-preview", "files-preview",
                           "background-preview", "counter-preview"};
  for (unsigned i = 0; i < 4; ++i)
    if (!strcmp(command, visuals[i]))
      return argc == 4 ? preview(root, argv[3], i + 3, 99) : 2;
  if (!strcmp(command, "preview") || !strcmp(command, "boot-preview") ||
      !strcmp(command, "color-preview"))
    return argc == 4 ? preview(root, argv[3],
                               !strcmp(command, "color-preview")
                                   ? 2
                                   : !strcmp(command, "boot-preview"),
                               POS_BOOT_HOLD_FRAME)
                     : 2;
  struct statvfs fs;
  if (statvfs(root, &fs))
    return 1;
  uint64_t free = (uint64_t)fs.f_bavail * fs.f_frsize;
  char archive[POS_FS_PATH];
  pos_fs_join(archive, root, POS_BACKUP_NAME);
  if (argc == 4)
    snprintf(archive, sizeof(archive), "%s", argv[3]);
  else if (strcmp(command, "backup") && strcmp(command, "backup-set") &&
           strcmp(command, "resume-backup") && strcmp(command, "scan")) {
    pos_error_t find_error = {0};
    if (pos_backup_find(root, archive, &find_error)) {
      fprintf(stderr, "%s\n", find_error.message);
      return 1;
    }
  }
  pos_backup_info_t info = {0};
  pos_error_t error = {0};
  int result = -1;
  if (!strcmp(command, "scan"))
    result = pos_backup_scan(root, &info, progress, NULL, &error);
  if (!strcmp(command, "backup"))
    result = pos_backup_create(root, "host", free, time(NULL), progress, NULL,
                               &error);
  if (!strcmp(command, "resume-backup"))
    result = pos_backup_set_resume(root, "host", free, time(NULL), progress,
                                   NULL, &error);
  if (!strcmp(command, "backup-set"))
    result = pos_backup_set_create(root, "host", free, time(NULL), progress,
                                   NULL, &error);
  if (!strcmp(command, "verify"))
    result = pos_backup_verify(archive, &info, progress, NULL, &error);
  if (!strcmp(command, "restore"))
    result =
        pos_backup_restore(root, archive, free, &info, progress, NULL, &error);
  if (result) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  if (strcmp(command, "backup"))
    printf("files=%u bytes=%llu required=%llu conflicts=%u\n",
           (unsigned)info.files, (unsigned long long)info.bytes,
           (unsigned long long)info.required, (unsigned)info.conflicts);
  return 0;
}
