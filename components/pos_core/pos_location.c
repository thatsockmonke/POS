#include "pos_location.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int hex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
static bool number(const char *s, double *value) {
  char *end;
  *value = strtod(s, &end);
  return *s && !*end && isfinite(*value);
}
static bool coordinate(const char *s, const char *dir, bool lat, double *out) {
  double raw;
  if (!number(s, &raw) || raw < 0 || strlen(dir) != 1 ||
      !(lat ? strchr("NS", *dir) : strchr("EW", *dir)))
    return false;
  double degrees = floor(raw / 100), minutes = raw - degrees * 100;
  if (minutes >= 60 || degrees > (lat ? 90 : 180))
    return false;
  *out = degrees + minutes / 60;
  if (*out > (lat ? 90 : 180))
    return false;
  if (*dir == 'S' || *dir == 'W')
    *out = -*out;
  return true;
}
/* Gregorian civil date -> Unix seconds, independent of timezone/timegm
 * availability. */
static int64_t epoch(const char *date, const char *clock) {
  if (strlen(date) != 6 || strlen(clock) < 6 ||
      strspn(date, "0123456789") != 6 || strspn(clock, "0123456789") < 6 ||
      (clock[6] && (clock[6] != '.' || !clock[7] ||
                    strspn(clock + 7, "0123456789") != strlen(clock + 7))))
    return 0;
  int day = (date[0] - '0') * 10 + date[1] - '0',
      month = (date[2] - '0') * 10 + date[3] - '0',
      year = 2000 + (date[4] - '0') * 10 + date[5] - '0';
  if (year >= 2080)
    year -= 100;
  int h = (clock[0] - '0') * 10 + clock[1] - '0',
      m = (clock[2] - '0') * 10 + clock[3] - '0',
      s = (clock[4] - '0') * 10 + clock[5] - '0';
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1 ||
      day > days[month - 1] + (month == 2 && year % 4 == 0) || h > 23 ||
      m > 59 || s > 60)
    return 0;
  year -= month <= 2;
  int era = year / 400;
  unsigned yoe = year - era * 400,
           doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1,
           doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return ((int64_t)era * 146097 + doe - 719468) * 86400 + h * 3600 + m * 60 + s;
}
static void sentence(pos_nmea_t *p, uint64_t now) {
  char *s = p->line;
  if (*s != '$')
    return;
  char *star = strchr(s, '*');
  if (!star || strlen(star) != 3 || hex(star[1]) < 0 || hex(star[2]) < 0)
    return;
  unsigned checksum = 0;
  for (char *c = s + 1; c < star; ++c)
    checksum ^= (unsigned char)*c;
  if (checksum != (unsigned)(hex(star[1]) * 16 + hex(star[2])))
    return;
  *star = 0;
  char *fields[24];
  unsigned n = 0;
  fields[n++] = s + 1;
  for (char *c = s + 1; *c; ++c)
    if (*c == ',') {
      *c = 0;
      if (n < 24)
        fields[n++] = c + 1;
      else
        return;
    }
  if (strlen(fields[0]) != 5)
    return;
  const char *type = fields[0] + 2;
  bool talker = !strncmp(fields[0], "GP", 2) || !strncmp(fields[0], "GN", 2) ||
                !strncmp(fields[0], "GL", 2) || !strncmp(fields[0], "GB", 2) ||
                !strncmp(fields[0], "BD", 2) || !strncmp(fields[0], "GA", 2) ||
                !strncmp(fields[0], "GQ", 2);
  if (!talker ||
      !((!strcmp(type, "RMC") && n >= 10) ||
        (!strcmp(type, "GGA") && n >= 10) || (!strcmp(type, "GSV") && n >= 4)))
    return;
  ++p->sentences;
  p->sentence_ms = now;
  double lat, lon;
  if (!strcmp(type, "RMC") && n >= 10) {
    p->location.received_ms = now;
    if (strcmp(fields[2], "A") ||
        !coordinate(fields[3], fields[4], true, &lat) ||
        !coordinate(fields[5], fields[6], false, &lon)) {
      p->location.fix = false;
      return;
    }
    p->location.fix = true;
    p->location.latitude = lat;
    p->location.longitude = lon;
    p->location.fix_ms = now;
    double value;
    if (number(fields[7], &value) && value >= 0)
      p->location.speed_kph = value * 1.852;
    if (number(fields[8], &value) && value >= 0 && value <= 360)
      p->location.course = value;
    p->location.utc = epoch(fields[9], fields[1]);
  } else if (!strcmp(type, "GGA") && n >= 10) {
    p->location.received_ms = now;
    if (!*fields[6] || atoi(fields[6]) < 1 || atoi(fields[6]) > 8 ||
        !coordinate(fields[2], fields[3], true, &lat) ||
        !coordinate(fields[4], fields[5], false, &lon)) {
      p->location.fix = false;
      return;
    }
    p->location.fix = true;
    p->location.fix_ms = now;
    p->location.latitude = lat;
    p->location.longitude = lon;
    int sats = atoi(fields[7]);
    if (sats >= 0 && sats <= 99)
      p->location.satellites = sats;
    double value;
    if (number(fields[8], &value) && value >= 0)
      p->location.hdop = value;
    if (number(fields[9], &value))
      p->location.altitude_m = value;
  } else if (!strcmp(type, "GSV") && n >= 4) {
    int count = atoi(fields[3]);
    if (count >= 0 && count <= 99)
      p->location.satellites_visible = count;
  }
}
void pos_nmea_feed(pos_nmea_t *p, const void *data, size_t length,
                   uint64_t now) {
  const unsigned char *bytes = data;
  for (size_t i = 0; i < length; ++i) {
    char c = bytes[i];
    if (c == '$') {
      p->used = 0;
      p->overflow = false;
    }
    if (c == '\n') {
      if (!p->overflow) {
        p->line[p->used] = 0;
        sentence(p, now);
      }
      p->used = 0;
      p->overflow = false;
    } else if (c != '\r') {
      if (p->used < sizeof(p->line) - 1)
        p->line[p->used++] = c;
      else
        p->overflow = true;
    }
  }
}
pos_location_t pos_location_snapshot(const pos_nmea_t *p, uint64_t now) {
  pos_location_t result = p->location;
  if (now < result.fix_ms || now - result.fix_ms > 5000)
    result.fix = false;
  return result;
}
double pos_location_distance(double a, double b, double c, double d) {
  if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d) ||
      fabs(a) > 90 || fabs(c) > 90 || fabs(b) > 180 || fabs(d) > 180)
    return NAN;
  const double rad = 0.017453292519943295;
  double x = sin((c - a) * rad / 2), y = sin((d - b) * rad / 2),
         v = x * x + cos(a * rad) * cos(c * rad) * y * y;
  return 6371000 * 2 * asin(sqrt(fmin(1, fmax(0, v))));
}
