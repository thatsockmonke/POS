#pragma once
#include "pos_maps.h"
#define POS_NAV_WALK 1u
#define POS_NAV_CYCLE 2u
#define POS_NAV_DRIVE 4u
#define POS_NAV_NAME 64
#define POS_NAV_RESULTS 8
typedef struct {
  char name[POS_NAV_NAME];
  double latitude, longitude;
} pos_nav_place_t;
typedef struct pos_nav_route pos_nav_route_t;
enum {
  POS_NAV_STRAIGHT,
  POS_NAV_LEFT,
  POS_NAV_RIGHT,
  POS_NAV_UTURN,
  POS_NAV_FINISH
};
typedef struct {
  unsigned segment, turn, remaining_m, next_m;
  bool off_route, arrived;
  char road[56];
} pos_nav_guidance_t;
/* Build a new, bound sidecar from the full navigation Overpass query. Temporary
 * external-sort runs and routing state live on SD; no whole-region RAM graph.
 */
int pos_nav_compile(const char *xml, const char *map, pos_io_progress_t, void *,
                    pos_error_t *);
int pos_nav_search(const char *map, const char *query, double lat, double lon,
                   unsigned mode, pos_nav_place_t results[POS_NAV_RESULTS],
                   unsigned *count, pos_io_progress_t, void *, pos_error_t *);
int pos_nav_calculate(const char *map, const char *destination, double from_lat,
                      double from_lon, double to_lat, double to_lon,
                      unsigned mode, pos_io_progress_t, void *, pos_error_t *);
pos_nav_route_t *pos_nav_open(const char *route, const char *map,
                              pos_io_progress_t, void *, pos_error_t *);
void pos_nav_close(pos_nav_route_t *);
int pos_nav_guide(pos_nav_route_t *, double lat, double lon, unsigned previous,
                  pos_nav_guidance_t *, pos_error_t *);
int pos_nav_raster(pos_nav_route_t *, double lat, double lon, unsigned width,
                   uint8_t pixels[POS_MAP_PIXELS], pos_error_t *);
void pos_nav_destination(pos_nav_route_t *, double *lat, double *lon);

unsigned pos_nav_mode(const pos_nav_route_t *);
