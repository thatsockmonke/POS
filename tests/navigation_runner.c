#include "pos_navigation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int progress(void *ctx, const char *phase, uint64_t a, uint64_t b) {
  (void)phase;
  (void)a;
  (void)b;
  return ctx != NULL;
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  pos_error_t e = {0};
  int rc = -1;
  pos_map_info_t m = {
      .latitude = 34.05, .longitude = -118.248, .width_m = 1000};
  strcpy(m.name, "Routing test");
  if (!strcmp(argv[1], "import") && argc >= 4)
    rc = pos_nav_import_osm(argv[2], argv[3], progress,
                            argc > 4 ? (void *)1 : NULL, &e);
  if (!strcmp(argv[1], "build") && argc >= 4) {
    if (argc >= 7) {
      m.latitude = strtod(argv[4], NULL);
      m.longitude = strtod(argv[5], NULL);
      m.width_m = atoi(argv[6]);
    }
    rc = pos_map_compile(argv[2], argv[3], &m, NULL, NULL, &e);
    if (!rc)
      rc = pos_nav_compile(argv[2], argv[3], NULL, NULL, &e);
  }
  if (!strcmp(argv[1], "navbuild") && argc >= 4)
    rc = pos_nav_compile(argv[2], argv[3], progress,
                         argc > 4 ? (void *)1 : NULL, &e);
  if (!strcmp(argv[1], "route") && argc >= 9)
    rc = pos_nav_calculate(argv[2], argv[3], strtod(argv[4], NULL),
                           strtod(argv[5], NULL), strtod(argv[6], NULL),
                           strtod(argv[7], NULL), (unsigned)atoi(argv[8]),
                           progress, argc > 9 ? (void *)1 : NULL, &e);
  if (!strcmp(argv[1], "search") && argc == 7) {
    pos_nav_place_t places[8];
    unsigned n = 0;
    rc = pos_nav_search(argv[2], argv[3], strtod(argv[4], NULL),
                        strtod(argv[5], NULL), atoi(argv[6]), places, &n, NULL,
                        NULL, &e);
    if (!rc)
      for (unsigned i = 0; i < n; ++i)
        printf("%s\t%.6f\t%.6f\n", places[i].name, places[i].latitude,
               places[i].longitude);
  }
  if ((!strcmp(argv[1], "guide") || !strcmp(argv[1], "render")) && argc >= 4) {
    pos_nav_route_t *r = pos_nav_open(argv[2], argv[3], NULL, NULL, &e);
    if (r) {
      if (!strcmp(argv[1], "guide") && argc >= 6) {
        pos_nav_guidance_t g;
        rc = pos_nav_guide(r, strtod(argv[4], NULL), strtod(argv[5], NULL),
                           argc > 6 ? (unsigned)strtoul(argv[6], NULL, 10)
                                    : UINT32_MAX,
                           &g, &e);
        if (!rc)
          printf("%u %u %u %u %u %u\t%s\n", g.segment, g.turn, g.next_m,
                 g.remaining_m, g.off_route, g.arrived, g.road);
      } else {
        uint8_t pixels[POS_MAP_PIXELS];
        rc = pos_nav_raster(r, m.latitude, m.longitude, 1000, pixels, &e);
        if (!rc) {
          unsigned bits = 0;
          for (unsigned i = 0; i < sizeof(pixels); ++i)
            for (unsigned j = 0; j < 8; ++j)
              bits += (pixels[i] >> j) & 1;
          printf("%u\n", bits);
        }
      }
      pos_nav_close(r);
    }
  }
  if (rc)
    fprintf(stderr, "%s\n", e.message);
  return rc ? 1 : 0;
}
