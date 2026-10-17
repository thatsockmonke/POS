#include "pos_core.h"
#include "pos_maps.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int cancel(void *ctx, const char *phase, uint64_t done, uint64_t total) {
  (void)phase;
  (void)done;
  (void)total;
  return ctx != NULL;
}
int main(int argc, char **argv) {
  pos_error_t e = {0};
  int result = -1;
  if (argc < 2)
    return 2;
  pos_map_info_t m = {
      .latitude = 34.0505, .longitude = -118.2475, .width_m = 1000};
  strcpy(m.name, "Test region");
  if (!strcmp(argv[1], "world") && argc >= 6) {
    uint8_t pixels[POS_MAP_PIXELS];
    double lat = strtod(argv[3], NULL), lon = strtod(argv[4], NULL);
    unsigned zoom = (unsigned)atoi(argv[5]);
    result = pos_world_raster(argv[2], lat, lon, zoom, pixels, &e);
    if (!result) {
      int x, y;
      pos_world_project(lat, lon, lat, lon, zoom, &x, &y);
      assert(x == 120 && y == 44);
      fwrite(pixels, 1, sizeof(pixels), stdout);
    }
  }
  if (!strcmp(argv[1], "world-geometry")) {
    int x, y;
    pos_world_project(0, -179, 0, 179, 0, &x, &y);
    assert(x == 121 && y == 44);
    pos_world_project(90, 180, 90, 180, 0, &x, &y);
    assert(x == 120 && y == 44);
    pos_world_project(-90, 180, 90, 180, 0, &x, &y);
    assert(y == 87);
    pos_world_project(NAN, 0, 0, 0, 0, &x, &y);
    assert(x == -32760);
    result = 0;
  }
  if (!strcmp(argv[1], "query")) {
    char url[512];
    if (argc > 2)
      m.latitude = strtod(argv[2], NULL);
    if (argc > 3)
      m.longitude = strtod(argv[3], NULL);
    if (argc > 4)
      m.width_m = atoi(argv[4]);
    result = pos_map_query(&m, argc > 5 ? (unsigned)atoi(argv[5]) : 0, url);
    if (!result)
      puts(url);
  }
  if (!strcmp(argv[1], "compile-4km"))
    m.width_m = 4000;
  if ((!strcmp(argv[1], "compile") || !strcmp(argv[1], "compile-4km")) && argc >= 4)
    result = pos_map_compile(argv[2], argv[3], &m, cancel,
                             argc > 4 ? (void *)1 : NULL, &e);
  if (!strcmp(argv[1], "raster") && argc >= 3) {
    uint8_t pixels[POS_MAP_PIXELS];
    result = pos_map_raster(argv[2], m.latitude, m.longitude, 1000, pixels,
                            cancel, argc > 3 ? (void *)1 : NULL, &e);
    if (!result) {
      unsigned n = 0;
      for (unsigned i = 0; i < sizeof(pixels); ++i)
        for (unsigned j = 0; j < 8; ++j)
          n += (pixels[i] >> j) & 1;
      printf("%u\n", n);
    }
  }
  if (!strcmp(argv[1], "geometry")) {
    int x, y;
    pos_map_project(34, -118, 34, -118, 1000, &x, &y);
    assert(x == 120 && y == 44);
    pos_map_project(34.001, -118, 34, -118, 1000, &x, &y);
    assert(x == 120 && y < 44);
    pos_map_project(34, -117.999, 34, -118, 1000, &x, &y);
    assert(x > 120 && y == 44);
    assert(fabs(pos_map_bearing(34, -118, 35, -118)) < .001);
    assert(fabs(pos_map_bearing(34, -118, 34, -117) - 90) < 1);
    assert(fabs(pos_map_bearing(34, -118, 33, -118) - 180) < .001);
    assert(fabs(pos_map_bearing(34, -118, 34, -119) - 270) < 1);
    pos_view_t v;
    pos_view_clear(&v, "Maps");
    uint8_t pixels[POS_MAP_PIXELS] = {0};
    v.map_pixels = pixels;
    assert(pos_view_pixel(&v, 1, 17) == 0x0842);
    pixels[(240 + 1) / 8] |= 1 << ((240 + 1) % 8);
    assert(pos_view_pixel(&v, 1, 17) == 0xce79);
    v.map_fix = true;
    v.map_gps_x = 1;
    v.map_gps_y = 1;
    assert(pos_view_pixel(&v, 1, 17) == 0x259f);
    v.map_waypoint = true;
    v.map_target_x = 20;
    v.map_target_y = 20;
    assert(pos_view_pixel(&v, 25, 36) == 0xf911);
    assert(pos_view_pixel(&v, 120, 60) == 0xfec0);
    uint8_t route_pixels[POS_MAP_PIXELS] = {0};
    v.map_route_pixels = route_pixels;
    route_pixels[(20 * 240 + 40) / 8] |= 1u << ((20 * 240 + 40) % 8);
    assert(pos_view_pixel(&v, 40, 36) == 0xfec0);
    v.map_fix = false;
    v.map_waypoint = false;
    v.map_region = true;
    v.map_left = 30;
    v.map_right = 210;
    v.map_top = 10;
    v.map_bottom = 70;
    assert(pos_view_pixel(&v, 30, 26) == 0xffff);
    assert(pos_view_pixel(&v, 100, 86) == 0xffff);
    assert(pos_view_pixel(&v, 29, 26) == 0x0842);
    v.map_left = -50;
    v.map_right = 290;
    v.map_top = -50;
    v.map_bottom = 140;
    assert(pos_view_pixel(&v, 0, 20) == 0x0842);
    v.map_region = false;
    uint32_t hashes[5];
    for (unsigned direction = 1; direction <= 5; ++direction) {
      v.map_turn = direction;
      uint32_t hash = 2166136261u;
      unsigned white = 0;
      for (int y = 20; y < 50; ++y)
        for (int x = 4; x < 37; ++x) {
          uint16_t color = pos_view_pixel(&v, x, y);
          white += color == 0xffff;
          hash = (hash ^ color) * 16777619u;
        }
      assert(white > 20);
      hashes[direction - 1] = hash;
      for (unsigned i = 0; i + 1 < direction; ++i)
        assert(hashes[i] != hash);
    }
    result = 0;
  }
  if (result)
    fprintf(stderr, "%s\n", e.message);
  return result ? 1 : 0;
}
