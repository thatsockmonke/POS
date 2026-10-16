#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  bool fix;
  double latitude, longitude, altitude_m, hdop, speed_kph, course;
  unsigned satellites, satellites_visible;
  int64_t utc;
  uint64_t fix_ms, received_ms;
} pos_location_t;
typedef struct {
  char line[160];
  size_t used;
  bool overflow;
  pos_location_t location;
  uint64_t sentence_ms;
  uint32_t sentences;
} pos_nmea_t;
void pos_nmea_feed(pos_nmea_t *parser, const void *data, size_t length,
                   uint64_t now_ms);
pos_location_t pos_location_snapshot(const pos_nmea_t *parser, uint64_t now_ms);
double pos_location_distance(double lat1, double lon1, double lat2,
                             double lon2);
