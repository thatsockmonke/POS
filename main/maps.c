#include "esp_random.h"
#include "esp_timer.h"
#include "pos_device.h"
#include "pos_maps.h"
#include "pos_navigation.h"
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
                         char map[POS_FS_PATH], char roads[POS_FS_PATH]) {
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
  const char *providers[] = {"OpenStreetMap (500 m)", "VK Maps (Russia)",
                             "Overpass API (Germany)", "Cancel"};
  int provider = pos_ui_menu("Choose map provider", providers, 4);
  if (provider < 0 || provider == 3)
    return;
  if (!provider && m.width_m != 500) {
    pos_ui_notice("Choose a smaller region",
                  "OpenStreetMap's direct API is limited here to a 500 m "
                  "square. Choose 500 m, or an Overpass provider for larger "
                  "areas. Dense areas can still exceed the download limit.");
    return;
  }
  char summary[400];
  snprintf(summary, sizeof(summary),
           "%.5f, %.5f\n%u m square\nRoads and paths, north up.\nUp to 8 MiB "
           "download.\nThis area is sent to this service: %s. OSM "
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
  const char *geometry = raw;
  if (!provider) {
    snprintf(relative, sizeof(relative), "POS/Maps/region-%08x-roads.osm", id);
    if (pos_fs_join(roads, root, relative) ||
        pos_nav_import_osm(raw, roads, progress, NULL, &e)) {
      pos_ui_notice("Map not prepared", e.message);
      return;
    }
    geometry = roads;
  }
  snprintf(relative, sizeof(relative), "POS/Maps/region-%08x.pmap", id);
  if (pos_fs_join(map, root, relative))
    return;
  if (pos_map_compile(geometry, map, &m, progress, NULL, &e))
    pos_ui_notice("Map not installed", e.message);
  else {
    if (pos_nav_compile(geometry, map, progress, NULL, &e)) {
      pos_ui_notice("Road view saved", e.message);
      return;
    }
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
                  "data stays in POS/Maps. Enter opens road search and offline "
                  "route planning. Choose a travel mode before routing.");
  }
}

static void download(const char *root) {
  char (*paths)[POS_FS_PATH] = calloc(3, POS_FS_PATH);
  if (!paths) {
    pos_ui_notice("Maps", "Not enough memory to prepare a download.");
    return;
  }
  download_run(root, paths[0], paths[1], paths[2]);
  free(paths);
}

typedef struct {
  pos_view_t view;
  uint8_t pixels[POS_MAP_PIXELS], route_pixels[POS_MAP_PIXELS];
  pos_nav_route_t *route;
  unsigned mode, segment;
} workspace_t;
static bool plan_route(const char *map, workspace_t *s, double start_lat,
                       double start_lon, double target_lat, double target_lon) {
  char *path = malloc(POS_FS_PATH);
  pos_error_t e = {0};
  if (!path)
    return false;
  if (snprintf(path, POS_FS_PATH, "%s.route-%08x", map,
               (unsigned)esp_random()) >= POS_FS_PATH) {
    free(path);
    return false;
  }
  pos_ui_notice("Offline route",
                "Start: current GPS fix, or saved region center when no fix is "
                "available. End: the nearest usable road point within 75m of "
                "your selection. Routes stay inside the cached graph. Check "
                "real signs and conditions.");
  const char *confirm[] = {"Calculate route", "Cancel"};
  if (pos_ui_menu(s->mode == POS_NAV_WALK    ? "Calculate walking route?"
                  : s->mode == POS_NAV_CYCLE ? "Calculate cycling route?"
                                             : "Calculate driving route?",
                  confirm, 2) != 0) {
    free(path);
    return false;
  }
  pos_ui_message("Offline routing",
                 "Calculating on microSD...\nEsc cancels safely.");
  if (pos_nav_calculate(map, path, start_lat, start_lon, target_lat, target_lon,
                        s->mode, progress, NULL, &e)) {
    pos_ui_notice("No route", e.message);
    free(path);
    return false;
  }
  pos_nav_route_t *next = pos_nav_open(path, map, progress, NULL, &e);
  free(path);
  if (!next) {
    pos_ui_notice("Route unavailable", e.message);
    return false;
  }
  pos_nav_close(s->route);
  s->route = next;
  s->segment = UINT32_MAX;
  return true;
}
static bool resume_route(const char *map, workspace_t *s) {
  char (*paths)[POS_FS_PATH] = calloc(3, POS_FS_PATH);
  if (!paths)
    return false;
  snprintf(paths[0], POS_FS_PATH, "%s", map);
  char *base = strrchr(paths[0], '/');
  if (!base) {
    free(paths);
    return false;
  }
  *base++ = 0;
  char prefix[128];
  if (snprintf(prefix, sizeof(prefix), "%s.route-", base) >=
      (int)sizeof(prefix)) {
    free(paths);
    return false;
  }
  DIR *d = opendir(paths[0]);
  struct dirent *entry;
  time_t latest = 0;
  bool found = false;
  if (d) {
    while ((entry = readdir(d))) {
      size_t n = strlen(prefix);
      if (strncmp(entry->d_name, prefix, n) || strlen(entry->d_name) != n + 8 ||
          strspn(entry->d_name + n, "0123456789abcdef") != 8)
        continue;
      if (pos_fs_join(paths[1], paths[0], entry->d_name))
        continue;
      struct stat st;
      if (!stat(paths[1], &st) && (!found || st.st_mtime >= latest)) {
        latest = st.st_mtime;
        strcpy(paths[2], paths[1]);
        found = true;
      }
    }
    closedir(d);
  }
  pos_error_t e = {0};
  pos_nav_route_t *next =
      found ? pos_nav_open(paths[2], map, progress, NULL, &e) : NULL;
  if (next) {
    pos_nav_close(s->route);
    s->route = next;
    s->mode = pos_nav_mode(next);
    s->segment = UINT32_MAX;
  } else
    pos_ui_notice("Saved route",
                  found ? e.message : "No saved route for this region yet.");
  free(paths);
  return next != NULL;
}
static bool road_search(const char *map, workspace_t *s, double *lat,
                        double *lon) {
  char query[64] = "";
  if (!pos_ui_text("Find a road", "Road name (part is OK)", query,
                   sizeof(query), false) ||
      !*query)
    return false;
  pos_nav_place_t *results = calloc(POS_NAV_RESULTS, sizeof(*results));
  if (!results)
    return false;
  unsigned count = 0;
  pos_error_t e = {0};
  bool selected = false;
  if (pos_nav_search(map, query, *lat, *lon, s->mode, results, &count, progress,
                     NULL, &e))
    pos_ui_notice("Search unavailable", e.message);
  else if (!count)
    pos_ui_notice(
        "No matching roads",
        "Try fewer letters, check the travel mode, or download a larger "
        "region. Search is offline and limited to this region.");
  else {
    const char *items[POS_NAV_RESULTS];
    for (unsigned i = 0; i < count; ++i)
      items[i] = results[i].name;
    int choice = pos_ui_menu("Nearby matching road names", items, count);
    if (choice >= 0) {
      *lat = results[choice].latitude;
      *lon = results[choice].longitude;
      selected = true;
    }
  }
  free(results);
  return selected;
}
static __attribute__((noinline)) void open_map(const char *path) {
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
  s->mode = POS_NAV_WALK;
  s->segment = UINT32_MAX;
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
      if (s->route &&
          pos_nav_raster(s->route, lat, lon, width, s->route_pixels, &e)) {
        pos_ui_notice("Guidance stopped", e.message);
        pos_nav_close(s->route);
        s->route = NULL;
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
      if (s->route)
        v->map_route_pixels = s->route_pixels;
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
      if (s->route) {
        if (gps.fix) {
          pos_nav_guidance_t g;
          if (pos_nav_guide(s->route, gps.latitude, gps.longitude, s->segment,
                            &g, &e)) {
            pos_nav_close(s->route);
            s->route = NULL;
            v->map_route_pixels = NULL;
            pos_view_line(v, 1, "Route read failed; reopen");
          } else {
            s->segment = g.segment;
            v->map_turn = g.off_route ? 0 : (uint8_t)(g.turn + 1);
            static const char *turns[] = {"Continue", "Turn left", "Turn right",
                                          "U-turn", "Arrived"};
            if (g.off_route)
              pos_view_line(v, 1, "Off route - Enter: recalc");
            else if (g.arrived)
              pos_view_line(v, 1, "Arrived at route end");
            else
              pos_view_line(v, 1, "%s in %um", turns[g.turn], g.next_m);
            pos_view_line(v, 13, "%s", *g.road ? g.road : "Unnamed road");
            pos_view_line(v, 14, "%um left   Enter: menu", g.remaining_m);
          }
        } else
          pos_view_line(v, 1, "No GPS fix - route saved");
      }
      if (!s->route)
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
      char mode_label[40];
      snprintf(mode_label, sizeof(mode_label), "Travel mode: %s",
               s->mode == 1   ? "Walking"
               : s->mode == 2 ? "Cycling"
                              : "Driving");
      const char *items[] = {"Route to map crosshair", "Search road name",
                             "Resume saved route",     mode_label,
                             "Clear guidance",         "Center saved region",
                             "Straight-line guide",    "Map help",
                             "Recalculate route",      "Back to map"};
      int c = pos_ui_menu("Map options", items, 10);
      if (c == 1) {
        if (road_search(path, s, &lat, &lon)) {
          follow = false;
          target_lat = lat;
          target_lon = lon;
          target = true;
          const char *choices[] = {"Route to this road", "Show on map"};
          if (pos_ui_menu("Road found", choices, 2) == 0)
            c = 0;
        }
      }
      if (c == 0 || c == 8) {
        double tolat = c == 8 ? target_lat : lat,
               tolon = c == 8 ? target_lon : lon;
        if (c == 8 && !target)
          pos_ui_notice("No destination", "Choose a road or map point first.");
        else if (c == 8 && !gps.fix)
          pos_ui_notice("GPS fix needed",
                        "Wait for a fresh position before recalculating. Your "
                        "saved route is retained.");
        else if (plan_route(path, s, gps.fix ? gps.latitude : m.latitude,
                            gps.fix ? gps.longitude : m.longitude, tolat,
                            tolon)) {
          pos_nav_destination(s->route, &target_lat, &target_lon);
          target = true;
          follow = gps.fix;
        }
      }
      if (c == 2 && resume_route(path, s)) {
        pos_nav_destination(s->route, &target_lat, &target_lon);
        target = true;
        follow = gps.fix;
      }
      if (c == 3) {
        const char *modes[] = {"Walking", "Cycling", "Driving"};
        int mode = pos_ui_menu("Travel mode (next route)", modes, 3);
        if (mode >= 0)
          s->mode = 1u << mode;
      }
      if (c == 4) {
        target = false;
        pos_nav_close(s->route);
        s->route = NULL;
      }
      if (c == 5) {
        lat = m.latitude;
        lon = m.longitude;
        width = m.width_m;
        follow = false;
      }
      if (c == 6) {
        target_lat = lat;
        target_lon = lon;
        target = true;
        pos_nav_close(s->route);
        s->route = NULL;
      }
      if (c == 7)
        pos_ui_notice(
            "Offline map controls",
            "Arrows pan; +/- zoom; C follows GPS. Enter: road search, travel "
            "mode, offline route planning and resume. Yellow line = route; "
            "arrow = next maneuver. Off route? Recalculate from a fresh fix. "
            "Route limits follow the downloaded graph and selected mode. Old "
            "0.6.3 maps need a fresh download for routing. Real signs and "
            "access rules take priority.");
      redraw = true;
    }
  }
  /* The compositor keeps a copy of the most recent view. Drop its borrowed
   * raster pointer before freeing the app workspace (including Home exits).
   */
  pos_view_clear(&s->view, "Maps");
  pos_device_show(&s->view);
  pos_nav_close(s->route);
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
static void world(const char *root) {
  struct world_view {
    pos_view_t view;
    uint8_t pixels[POS_MAP_PIXELS];
  };
  struct world_view *s = calloc(1, sizeof(*s));
  if (!s) {
    pos_ui_notice("World map", "Not enough free memory.");
    return;
  }
  char path[POS_FS_PATH];
  if (pos_fs_join(path, root, "POS/System/Assets/world.map")) {
    free(s);
    return;
  }
  pos_location_t gps = pos_expansion_location();
  double lat = gps.fix ? gps.latitude : 0, lon = gps.fix ? gps.longitude : 0;
  bool follow = true, redraw = true;
  unsigned zoom = 0;
  uint32_t generation = 0;
  int64_t refreshed = 0;
  while (!pos_home_pending()) {
    gps = pos_expansion_location();
    int gx, gy;
    pos_world_project(gps.latitude, gps.longitude, lat, lon, zoom, &gx, &gy);
    if (follow && gps.fix && (abs(gx - 120) >= 1 || abs(gy - 44) >= 1)) {
      lat = gps.latitude;
      lon = gps.longitude;
      redraw = true;
    }
    if (redraw) {
      pos_error_t e = {0};
      if (pos_world_raster(path, lat, lon, zoom, s->pixels, &e)) {
        pos_ui_notice("World map unavailable", e.message);
        break;
      }
      ++generation;
      redraw = false;
      refreshed = 0;
    }
    int64_t now = esp_timer_get_time();
    if (!refreshed || now - refreshed >= 500000) {
      pos_view_t *v = &s->view;
      pos_view_clear(v, "World / North up");
      pos_ui_style(v);
      v->map_pixels = s->pixels;
      v->map_world = true;
      v->map_generation = generation;
      v->map_fix = gps.fix;
      int x, y;
      pos_world_project(gps.latitude, gps.longitude, lat, lon, zoom, &x, &y);
      v->map_gps_x = x;
      v->map_gps_y = y;
      pos_view_line(v, 1, "%s  %s  %ux", gps.fix ? "GPS" : "Waiting for fix",
                    follow ? "follow" : "pan", 1u << zoom);
      pos_view_line(v, 13, "%.4f %.4f", lat, lon);
      pos_view_line(v, 14, "Arrows: pan +/-: zoom C: GPS");
      pos_view_line(v, 15, "0: whole planet Enter: menu");
      pos_device_show(v);
      refreshed = now;
    }
    pos_event_t event;
    if (!pos_device_event(&event, 50) || !event.pressed)
      continue;
    int key = pos_navigation(&event);
    if (key == POS_KEY_HOME || key == POS_KEY_ESCAPE || key == POS_KEY_ENTER)
      break;
    double step = 30. / (1u << zoom);
    if (key == POS_KEY_LEFT || key == POS_KEY_RIGHT) {
      lon += key == POS_KEY_LEFT ? -step : step;
      lon = fmod(lon + 540., 360.) - 180.;
      follow = false;
      redraw = true;
    }
    if (key == POS_KEY_UP || key == POS_KEY_DOWN) {
      lat = fmax(-90, fmin(90, lat + (key == POS_KEY_UP ? step : -step)));
      follow = false;
      redraw = true;
    }
    if ((key == '+' || key == '=') && zoom < 8) {
      ++zoom;
      redraw = true;
    }
    if (key == '-' && zoom) {
      --zoom;
      redraw = true;
    }
    if (key == '0') {
      zoom = 0;
      redraw = true;
    }
    if (key == 'c' || key == 'C') {
      follow = true;
      redraw = true;
    }
  }
  /* Device retains borrowed bitmap pointers until a non-map view is shown. */
  pos_view_clear(&s->view, "Maps");
  pos_ui_style(&s->view);
  pos_device_show(&s->view);
  free(s);
}

void pos_ui_maps(const char *root) {
  if (!pos_expansion_maps_available()) {
    pos_ui_notice("Maps need the GPS cap",
                  "No GPS receiver detected. Fit the LoRa/GPS cap, restart, "
                  "and check Settings > Expansion hardware. A satellite fix is "
                  "only needed for your live position.");
    return;
  }
  int selected = 4;
  while (!pos_home_pending()) {
    const char *items[] = {
        "Saved regions (offline)", "Download a region", "GPS status",
        "About map data",          "World map",         "Back"};
    int choice = pos_ui_menu_from("Maps", items, 6, selected);
    if (choice < 0 || choice == 5)
      return;
    selected = choice;
    if (choice == 4)
      world(root);
    if (choice == 0)
      saved(root);
    if (choice == 1)
      download(root);
    if (choice == 2)
      pos_ui_location(root);
    if (choice == 3)
      pos_ui_notice("Map data & credits",
                    "World overview: Natural Earth, public domain. Regional "
                    "roads: (c) OpenStreetMap contributors. "
                    "https://www.openstreetmap.org/copyright. ODbL 1.0. "
                    "Downloads use a public Overpass service; availability is "
                    "not guaranteed. Raw .osm files remain in POS/Maps. Small "
                    "areas only. Offline routing uses the selected travel mode "
                    "and downloaded restrictions.");
  }
}
#else
void pos_ui_maps(const char *root) { (void)root; }
#endif
