#include "esp_random.h"
#include "esp_timer.h"
#include "pos_device.h"
#include "pos_maps.h"
#include "pos_network_service.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef POS_INSTALLER
static int progress(void *ctx, const char *phase, uint64_t done,
                    uint64_t total) {
  return pos_ui_progress(ctx, phase, "Map region", done, total);
}
static bool number(const char *title, const char *label, double *value,
                   double limit) {
  char text[24];
  snprintf(text, sizeof(text), "%.6f", *value);
  if (!pos_ui_text(title, label, text, sizeof(text), false))
    return false;
  char *end;
  double next = strtod(text, &end);
  if (end == text || *end || !isfinite(next) || fabs(next) > limit) {
    pos_ui_notice("Invalid coordinates",
                  "Enter decimal degrees, for example 34.0522 and -118.2437.");
    return false;
  }
  *value = next;
  return true;
}
static void download_run(const char *root, char raw[POS_FS_PATH],
                         char map[POS_FS_PATH]) {
  pos_location_t gps = pos_expansion_location();
  pos_map_info_t m = {
      .latitude = gps.latitude, .longitude = gps.longitude, .width_m = 1000};
  strcpy(m.name, "My region");
  const char *places[] = {"Around my GPS position", "Enter coordinates",
                          "Back"};
  int choice = pos_ui_menu("Download a map", places, 3);
  if (choice < 0 || choice == 2)
    return;
  if (!choice && !gps.fix) {
    pos_ui_notice("Waiting for GPS fix",
                  "Take the device outdoors or enter coordinates. A detected "
                  "receiver alone does not establish your position.");
    return;
  }
  if (choice == 1 &&
      (!number("Map center", "Latitude (-80 to 80)", &m.latitude, 80) ||
       !number("Map center", "Longitude (-180 to 180)", &m.longitude, 180)))
    return;
  const char *sizes[] = {"500 m square", "1 km square", "2 km square",
                         "4 km square", "Cancel"};
  choice = pos_ui_menu("Choose region size", sizes, 5);
  if (choice < 0 || choice == 4)
    return;
  m.width_m = 500u << choice;
  if (!pos_ui_text("Name this region", "Map name", m.name, sizeof(m.name),
                   false))
    return;
  if (!pos_map_region_valid(&m)) {
    pos_ui_notice("Invalid region",
                  "Use a short ASCII name and coordinates between -80/+80 "
                  "latitude, away from the date line.");
    return;
  }
  const char *providers[] = {"VK Maps (Russia)", "Private.coffee (Austria)",
                             "Overpass API (Germany)", "Cancel"};
  int provider = pos_ui_menu("Choose map provider", providers, 4);
  if (provider < 0 || provider == 3)
    return;
  char summary[400];
  snprintf(
      summary, sizeof(summary),
      "%.5f, %.5f\n%u m square\nRoads and paths, north up.\nUp to 2 MiB "
      "download.\nThis area is sent to this public Overpass service: %s. OSM "
      "contributors / ODbL. No background location sharing.",
      m.latitude, m.longitude, m.width_m, pos_map_providers[provider]);
  pos_ui_notice("Review map download", summary);
  const char *options[] = {"Download region over Wi-Fi", "Cancel"};
  if (pos_ui_menu("Save for offline use?", options, 2) != 0)
    return;
  if (!pos_network_status().connected)
    pos_ui_wifi(root);
  if (pos_home_pending() || !pos_network_status().connected)
    return;
  if (time(NULL) < 1704067200 && pos_network_sync_time()) {
    pos_ui_notice("Time needed for HTTPS",
                  "Sync time over Wi-Fi or set UTC in Settings. Certificate "
                  "validation remains enabled.");
    return;
  }
  char relative[80], url[512];
  unsigned id = esp_random();
  snprintf(relative, sizeof(relative), "POS/Maps/region-%08x.osm", id);
  if (pos_fs_join(raw, root, relative) ||
      pos_map_query(&m, (unsigned)provider, url))
    return;
  char folder[POS_FS_PATH];
  if (pos_fs_join(folder, root, "POS/Maps") || pos_fs_mkdirs(folder))
    return;
  pos_error_t e = {0};
  pos_ui_message("Downloading map",
                 "Waiting for map server...\nPublic services can be busy.\nEsc "
                 "cancels during transfer.");
  if (pos_network_https_limit(url, "GET", NULL, NULL, root, relative, NULL,
                              progress, NULL, &e, POS_MAP_DOWNLOAD_LIMIT)) {
    pos_ui_notice("Map download failed", e.message);
    return;
  }
  snprintf(relative, sizeof(relative), "POS/Maps/region-%08x.pmap", id);
  if (pos_fs_join(map, root, relative))
    return;
  if (pos_map_compile(raw, map, &m, progress, NULL, &e))
    pos_ui_notice("Map not installed", e.message);
  else {
    const char *notice =
        "Map data: (c) OpenStreetMap "
        "contributors.\nhttps://www.openstreetmap.org/copyright\nOriginal .osm "
        "and derived .pmap databases: Open Database License "
        "1.0.\nhttps://opendatacommons.org/licenses/odbl/1-0/\nKeep this "
        "notice and source .osm files when sharing maps.\n";
    if (!pos_fs_join(folder, root, "POS/Maps/Map-data-license.txt"))
      (void)pos_fs_write_new(folder, notice, strlen(notice));
    pos_ui_notice("Map saved",
                  "Available in Saved regions without Internet. Original OSM "
                  "data stays in POS/Maps. Enter on a map sets a waypoint; "
                  "guidance is straight-line, not turn-by-turn.");
  }
}

static void download(const char *root) {
  char (*paths)[POS_FS_PATH] = calloc(2, POS_FS_PATH);
  if (!paths) {
    pos_ui_notice("Maps", "Not enough memory to prepare a download.");
    return;
  }
  download_run(root, paths[0], paths[1]);
  free(paths);
}

typedef struct {
  pos_view_t view;
  uint8_t pixels[POS_MAP_PIXELS];
} workspace_t;
static void open_map(const char *path) {
  pos_map_info_t m;
  pos_error_t e = {0};
  if (pos_map_inspect(path, &m, &e)) {
    pos_ui_notice("Map unavailable", e.message);
    return;
  }
  workspace_t *s = calloc(1, sizeof(*s));
  if (!s) {
    pos_ui_notice("Maps", "Not enough memory to open this map.");
    return;
  }
  double lat = m.latitude, lon = m.longitude, target_lat = 0, target_lon = 0;
  unsigned width = m.width_m, generation = 0;
  bool redraw = true, follow = false, target = false;
  int64_t refreshed = 0;
  while (!pos_home_pending()) {
    pos_location_t gps = pos_expansion_location();
    if (follow && gps.fix) {
      int x, y;
      pos_map_project(gps.latitude, gps.longitude, lat, lon, width, &x, &y);
      if (abs(x - 120) > 12 || abs(y - 44) > 12) {
        lat = gps.latitude;
        lon = gps.longitude;
        redraw = true;
      }
    }
    if (redraw) {
      pos_ui_message("Opening offline map", "Reading roads from microSD...");
      if (pos_map_raster(path, lat, lon, width, s->pixels, progress, NULL,
                         &e)) {
        pos_ui_notice("Map unavailable", e.message);
        break;
      }
      ++generation;
      redraw = false;
      refreshed = 0;
    }
    int64_t now = esp_timer_get_time();
    if (!refreshed || now - refreshed >= 500000) {
      pos_view_t *v = &s->view;
      pos_view_clear(v, "Maps / North up");
      pos_ui_style(v);
      v->map_pixels = s->pixels;
      v->map_generation = generation;
      v->map_fix = gps.fix;
      v->map_waypoint = target;
      int x, y;
      pos_map_project(gps.latitude, gps.longitude, lat, lon, width, &x, &y);
      v->map_gps_x = x;
      v->map_gps_y = y;
      pos_map_project(target_lat, target_lon, lat, lon, width, &x, &y);
      v->map_target_x = x;
      v->map_target_y = y;
      if (target && gps.fix) {
        double distance = pos_location_distance(gps.latitude, gps.longitude,
                                                target_lat, target_lon);
        unsigned bearing = (unsigned)pos_map_bearing(
            gps.latitude, gps.longitude, target_lat, target_lon);
        static const char *dirs[] = {"N", "NE", "E", "SE",
                                     "S", "SW", "W", "NW"};
        pos_view_line(v, 1, "%.0fm %s %u deg%s", distance,
                      dirs[((bearing + 22) / 45) % 8], bearing,
                      distance < 15 ? " near" : "");
      } else
        pos_view_line(v, 1, "%s %um %s", gps.fix ? "GPS" : "No fix", width,
                      follow ? "follow" : "pan");
      pos_view_line(v, 13, "%.5f %.5f", lat, lon);
      pos_view_line(v, 14, "+/- zoom C:GPS Enter:menu");
      pos_view_line(v, 15, "(c) OpenStreetMap contributors");
      pos_device_show(v);
      refreshed = now;
    }
    pos_event_t event;
    if (!pos_device_event(&event, 100))
      continue;
    int key = pos_navigation(&event);
    if (key == POS_KEY_HOME || key == POS_KEY_ESCAPE)
      break;
    if (!event.pressed)
      continue;
    if (key == POS_KEY_UP || key == POS_KEY_DOWN || key == POS_KEY_LEFT ||
        key == POS_KEY_RIGHT) {
      double step = width / 4.0 / 111195.08;
      if (key == POS_KEY_UP)
        lat += step;
      if (key == POS_KEY_DOWN)
        lat -= step;
      if (key == POS_KEY_LEFT)
        lon -= step / cos(lat * 3.14159265358979323846 / 180);
      if (key == POS_KEY_RIGHT)
        lon += step / cos(lat * 3.14159265358979323846 / 180);
      lat = fmax(-80, fmin(80, lat));
      lon = fmax(-179.9, fmin(179.9, lon));
      follow = false;
      redraw = true;
    }
    if (key == '+' || key == '=' || key == '-') {
      width = key == '-' ? width * 2 : width / 2;
      if (width < 125)
        width = 125;
      if (width > 16000)
        width = 16000;
      redraw = true;
    }
    if (key == 'c' || key == 'C') {
      if (!gps.fix)
        pos_ui_notice("No GPS fix", "Wait outdoors for a fresh fix. Offline "
                                    "pan and zoom still work.");
      else {
        lat = gps.latitude;
        lon = gps.longitude;
        follow = true;
        redraw = true;
      }
    }
    if (key == POS_KEY_ENTER) {
      const char *items[] = {"Guide to map crosshair", "Clear waypoint",
                             "Center saved region", "Map help", "Back to map"};
      int c = pos_ui_menu("Map options", items, 5);
      if (c == 0) {
        target_lat = lat;
        target_lon = lon;
        target = true;
      }
      if (c == 1)
        target = false;
      if (c == 2) {
        lat = m.latitude;
        lon = m.longitude;
        width = m.width_m;
        follow = false;
      }
      if (c == 3)
        pos_ui_notice(
            "Offline map controls",
            "Arrows pan. + / - zoom. C follows GPS. Enter sets the center "
            "crosshair as your destination. Blue = you; pink = waypoint. "
            "Guidance is straight-line, not a safe walking/driving route. No "
            "turn instructions, terrain or road names yet. Outside the "
            "downloaded area the map can be blank.");
      redraw = true;
    }
  }
  /* The compositor keeps a copy of the most recent view. Drop its borrowed
   * raster pointer before freeing the app workspace (including Home exits).
   */
  pos_view_clear(&s->view, "Maps");
  pos_device_show(&s->view);
  free(s);
}
static void saved(const char *root) {
  char folder[POS_FS_PATH];
  if (pos_fs_join(folder, root, "POS/Maps"))
    return;
  DIR *d = opendir(folder);
  if (!d) {
    pos_ui_notice("Saved regions", "No maps yet. Choose Download a region.");
    return;
  }
  /* Pages rescan the directory rather than retaining an unbounded catalog. */
  unsigned page = 0;
  while (!pos_home_pending()) {
    rewinddir(d);
    char names[8][64], labels[8][40];
    const char *items[11];
    unsigned n = 0, seen = 0;
    bool more = false;
    struct dirent *entry;
    while ((entry = readdir(d))) {
      size_t length = strlen(entry->d_name);
      if (length < 6 || length >= sizeof(names[0]) ||
          strcmp(entry->d_name + length - 5, ".pmap"))
        continue;
      char full[POS_FS_PATH], relative[120];
      pos_map_info_t m;
      pos_error_t e = {0};
      snprintf(relative, sizeof(relative), "POS/Maps/%.63s", entry->d_name);
      if (pos_fs_join(full, root, relative) || pos_map_inspect(full, &m, &e))
        continue;
      if (seen++ < page * 8)
        continue;
      if (n == 8) {
        more = true;
        break;
      }
      strcpy(names[n], entry->d_name);
      snprintf(labels[n], sizeof(labels[n]), "%.32s", m.name);
      items[n] = labels[n];
      ++n;
    }
    unsigned next = n, previous = n;
    if (more) {
      next = n;
      items[n++] = "Next page";
    }
    if (page) {
      previous = n;
      items[n++] = "Previous page";
    }
    unsigned back = n;
    items[n++] = "Back";
    int choice = pos_ui_menu("Saved regions", items, n);
    if (choice < 0 || (unsigned)choice == back)
      break;
    if (more && (unsigned)choice == next) {
      ++page;
      continue;
    }
    if (page && (unsigned)choice == previous) {
      --page;
      continue;
    }
    char full[POS_FS_PATH], relative[120];
    snprintf(relative, sizeof(relative), "POS/Maps/%s", names[choice]);
    if (!pos_fs_join(full, root, relative))
      open_map(full);
  }
  closedir(d);
}
void pos_ui_maps(const char *root) {
  if (!pos_expansion_maps_available()) {
    pos_ui_notice("Maps need the GPS cap",
                  "No GPS receiver detected. Fit the LoRa/GPS cap, restart, "
                  "and check Settings > Expansion hardware. A satellite fix is "
                  "only needed for your live position.");
    return;
  }
  int selected = 0;
  while (!pos_home_pending()) {
    const char *items[] = {"Saved regions (offline)", "Download a region",
                           "GPS status", "About map data", "Back"};
    int choice = pos_ui_menu_from("Maps", items, 5, selected);
    if (choice < 0 || choice == 4)
      return;
    selected = choice;
    if (choice == 0)
      saved(root);
    if (choice == 1)
      download(root);
    if (choice == 2)
      pos_ui_location(root);
    if (choice == 3)
      pos_ui_notice("OpenStreetMap / ODbL",
                    "Map data: (c) OpenStreetMap contributors. "
                    "https://www.openstreetmap.org/copyright. ODbL 1.0. "
                    "Downloads use a public Overpass service; availability is "
                    "not guaranteed. Raw .osm files remain in POS/Maps. Small "
                    "areas only; no turn-by-turn routing yet.");
  }
}
#else
void pos_ui_maps(const char *root) { (void)root; }
#endif
