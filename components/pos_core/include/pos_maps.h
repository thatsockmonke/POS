#pragma once
#include "pos_fs.h"
#include "pos_install.h"
#define POS_MAP_WIDTH 240
#define POS_MAP_HEIGHT 88
#define POS_MAP_PIXELS (POS_MAP_WIDTH * POS_MAP_HEIGHT / 8)
#define POS_MAP_DOWNLOAD_LIMIT (2u * 1024u * 1024u)
#define POS_MAP_SEGMENTS 30000u
/* Small north-up regions, away from polar/date-line projection singularities.
 */
typedef struct {
  double latitude, longitude;
  unsigned width_m, segments;
  char name[33];
} pos_map_info_t;
bool pos_map_region_valid(const pos_map_info_t *);
int pos_map_query(const pos_map_info_t *, unsigned provider, char url[512]);
/* Source remains ordinary OSM XML. Compile to a new, checksummed .pmap file;
 * neither an existing map nor its source is replaced. */
int pos_map_compile(const char *source, const char *destination,
                    const pos_map_info_t *, pos_io_progress_t, void *,
                    pos_error_t *);
int pos_map_inspect(const char *path, pos_map_info_t *, pos_error_t *);
/* Stream every record into a small monochrome viewport; validates CRC while
 * reading. Never allocates the region in RAM. Progress may cancel. */
int pos_map_raster(const char *path, double latitude, double longitude,
                   unsigned width_m, uint8_t pixels[POS_MAP_PIXELS],
                   pos_io_progress_t, void *, pos_error_t *);
void pos_map_project(double latitude, double longitude, double center_lat,
                     double center_lon, unsigned width_m, int *x, int *y);
double pos_map_bearing(double latitude, double longitude, double dest_lat,
                       double dest_lon);

/* Explicit provider choice; downloads never fall back to another host silently.
 */
extern const char *const pos_map_providers[3];

void pos_map_line(uint8_t pixels[POS_MAP_PIXELS], int x, int y, int xx, int yy);
