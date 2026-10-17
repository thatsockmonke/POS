#include "pos_maps.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEADER 64u
#define METERS 111195.08
#define RAD (3.14159265358979323846 / 180.0)
static int fail(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static uint32_t get(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static void put(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(v >> (8 * i));
}
bool pos_map_region_valid(const pos_map_info_t *m) {
  if (!m || !isfinite(m->latitude) || !isfinite(m->longitude) ||
      fabs(m->latitude) > 80 || m->width_m < 500 || m->width_m > 4000 ||
      !memchr(m->name, 0, sizeof(m->name)) || !*m->name)
    return false;
  double half = m->width_m / (2 * METERS * cos(m->latitude * RAD));
  if (fabs(m->longitude) + half >= 180)
    return false;
  for (const unsigned char *p = (const unsigned char *)m->name; *p; ++p)
    if (*p < 32 || *p > 126)
      return false;
  return true;
}
const char *const pos_map_providers[3] = {
    "https://api.openstreetmap.org/api/0.6/map",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
    "https://overpass-api.de/api/interpreter"};
int pos_map_query(const pos_map_info_t *m, unsigned provider, char url[512]) {
  if (!pos_map_region_valid(m) || provider >= 3)
    return -1;
  double dy = m->width_m / (2 * METERS), dx = dy / cos(m->latitude * RAD);
  if (!provider) {
    /* One explicit small bbox (up to 4 km), never bulk region scraping.
     * Resolve/filter references on SD; server density limits still apply. */
    int n = snprintf(url, 512, "%s?bbox=%.6f,%.6f,%.6f,%.6f",
                     pos_map_providers[0], m->longitude - dx, m->latitude - dy,
                     m->longitude + dx, m->latitude + dy);
    return n < 0 || n >= 512 ? -1 : 0;
  }
  /* Explicitly bounded vector query; no bulk tile scraping or location upload
   * in the background. Percent-encode every non-unreserved query byte. */
  char query[256];
  snprintf(
      query, sizeof(query),
      "[out:xml][timeout:25][maxsize:67108864];way[\"highway\"](%.6f,%.6f,%"
      ".6f,%.6f)->.r;rel(bw.r)[type=restriction];out body;.r out "
      "geom;node(w.r)[~\".\"~\".\"];out tags;",
      m->latitude - dy, m->longitude - dx, m->latitude + dy, m->longitude + dx);
  const char *base = pos_map_providers[provider];
  size_t n = strlen(base);
  memcpy(url, base, n);
  memcpy(url + n, "?data=", 6);
  n += 6;
  static const char hex[] = "0123456789ABCDEF";
  for (const unsigned char *p = (const unsigned char *)query; *p; ++p) {
    bool simple = isalnum(*p) || strchr("-._~", *p);
    if (n + (simple ? 1 : 3) >= 512)
      return -1;
    if (simple)
      url[n++] = *p;
    else {
      url[n++] = '%';
      url[n++] = hex[*p >> 4];
      url[n++] = hex[*p & 15];
    }
  }
  url[n] = 0;
  return 0;
}
/* Attribute lookup recognizes boundaries and quotes, never a substring in
 * another attribute. Only numeric nd coordinates are interpreted. */
static int coordinate(const char *tag, const char *key, double *number) {
  size_t k = strlen(key);
  const char *p = tag;
  while (*p && !isspace((unsigned char)*p))
    ++p;
  while (*p) {
    while (isspace((unsigned char)*p))
      ++p;
    const char *name = p;
    while (isalnum((unsigned char)*p) || *p == '_' || *p == ':')
      ++p;
    size_t n = (size_t)(p - name);
    if (!n)
      break;
    while (isspace((unsigned char)*p))
      ++p;
    if (*p++ != '=')
      return -1;
    while (isspace((unsigned char)*p))
      ++p;
    char quote = *p++;
    if (quote != '\'' && quote != '"')
      return -1;
    const char *value = p;
    while (*p && *p != quote)
      ++p;
    if (!*p)
      return -1;
    if (n == k && !memcmp(name, key, k)) {
      char *end;
      *number = strtod(value, &end);
      return end == p && end != value && isfinite(*number) ? 0 : -1;
    }
    ++p;
  }
  return -1;
}
static void header(uint8_t h[HEADER], const pos_map_info_t *m, uint32_t crc) {
  memset(h, 0, HEADER);
  memcpy(h, "PMP1", 4);
  put(h + 4, (uint32_t)(int32_t)llround(m->latitude * 1000000));
  put(h + 8, (uint32_t)(int32_t)llround(m->longitude * 1000000));
  put(h + 12, m->width_m);
  put(h + 16, m->segments);
  put(h + 20, crc);
  memcpy(h + 24, m->name, strlen(m->name));
}
static int read_header(FILE *f, pos_map_info_t *m, uint8_t h[HEADER]) {
  if (fread(h, 1, HEADER, f) != HEADER || memcmp(h, "PMP1", 4) ||
      !memchr(h + 24, 0, 33))
    return -1;
  for (unsigned i = 57; i < HEADER; ++i)
    if (h[i])
      return -1;
  m->latitude = (int32_t)get(h + 4) / 1000000.;
  m->longitude = (int32_t)get(h + 8) / 1000000.;
  m->width_m = get(h + 12);
  m->segments = get(h + 16);
  memcpy(m->name, h + 24, 33);
  return !pos_map_region_valid(m) || !m->segments ||
                 m->segments > POS_MAP_SEGMENTS
             ? -1
             : 0;
}
int pos_map_inspect(const char *path, pos_map_info_t *m, pos_error_t *e) {
  FILE *f = fopen(path, "rb");
  uint8_t h[HEADER];
  uint32_t size;
  int bad = !f || read_header(f, m, h) || pos_fs_file_size(path, &size) ||
            size != HEADER + m->segments * 16;
  if (f)
    fclose(f);
  return bad ? fail(e, "Invalid or incomplete map. Download it again.") : 0;
}
int pos_map_compile(const char *source, const char *destination,
                    const pos_map_info_t *region, pos_io_progress_t progress,
                    void *ctx, pos_error_t *e) {
  uint32_t size;
  if (!pos_map_region_valid(region) || pos_fs_file_size(source, &size) ||
      !size || size > POS_MAP_DOWNLOAD_LIMIT ||
      strlen(destination) > POS_FS_PATH - 6)
    return fail(e, "Invalid region or oversized map download.");
  char part[POS_FS_PATH];
  snprintf(part, sizeof(part), "%s.part", destination);
  FILE *in = fopen(source, "rb"), *out = NULL;
  uint8_t *buffers = malloc(8192);
  int result = -1;
  if (!in || !buffers) {
    fail(e, "Cannot open map or allocate map buffers.");
    goto done;
  }
  if (!access(destination, F_OK) || !(out = fopen(part, "wbx"))) {
    fail(e, "Map destination exists. Choose a new region download.");
    goto done;
  }
  setvbuf(in, (char *)buffers, _IOFBF, 4096);
  setvbuf(out, (char *)buffers + 4096, _IOFBF, 4096);
  pos_map_info_t m = *region;
  m.segments = 0;
  uint8_t h[HEADER] = {0};
  if (fwrite(h, 1, HEADER, out) != HEADER)
    goto io_error;
  char tag[384];
  size_t used = 0;
  bool inside = false, overflow = false, osm = false, closed = false,
       way = false, have = false;
  int32_t lastlat = 0, lastlon = 0;
  uint32_t read = 0;
  int c, quote = 0;
  while ((c = fgetc(in)) != EOF) {
    ++read;
    if (!(read % 4096) && progress &&
        progress(ctx, "Preparing map", read, size)) {
      fail(e, "Map preparation cancelled; existing maps preserved.");
      goto done;
    }
    if (c == '<' && !quote) {
      if (inside)
        goto invalid;
      inside = true;
      overflow = false;
      used = 0;
      continue;
    }
    if (!inside) {
      if (closed && !isspace((unsigned char)c))
        goto invalid;
      continue;
    }
    if (inside && (c == '\'' || c == '"')) {
      if (quote == c)
        quote = 0;
      else if (!quote)
        quote = c;
    }
    if (c != '>' || quote) {
      if (used < sizeof(tag) - 1)
        tag[used++] = (char)c;
      else
        overflow = true;
      continue;
    }
    inside = false;
    tag[used] = 0;
    if (closed)
      goto invalid;
    if (overflow) { /* Large descriptive tags are not geometry. */
      if (!strncmp(tag, "tag ", 4) || !strncmp(tag, "meta ", 5))
        continue;
      goto invalid;
    }
    if (!strncmp(tag, "osm ", 4)) {
      if (osm)
        goto invalid;
      osm = true;
    } else if (!strcmp(tag, "/osm")) {
      if (!osm || way)
        goto invalid;
      closed = true;
    } else if (!strncmp(tag, "remark", 6) || !strncmp(tag, "error", 5) ||
               !strncmp(tag, "!DOCTYPE", 8))
      goto invalid;
    else if (!strncmp(tag, "way ", 4)) {
      if (!osm || way)
        goto invalid;
      way = true;
      have = false;
    } else if (!strcmp(tag, "/way")) {
      if (!way)
        goto invalid;
      way = false;
      have = false;
    } else if (!strncmp(tag, "nd ", 3)) {
      double lat, lon;
      if (!way || coordinate(tag, "lat", &lat) ||
          coordinate(tag, "lon", &lon) || fabs(lat) > 90 || fabs(lon) > 180)
        goto invalid;
      int32_t a = (int32_t)llround(lat * 1000000),
              b = (int32_t)llround(lon * 1000000);
      if (have && (a != lastlat || b != lastlon)) {
        if (++m.segments > POS_MAP_SEGMENTS) {
          fail(e, "Too many roads. Download a smaller area.");
          goto done;
        }
        uint8_t record[16];
        put(record, lastlat);
        put(record + 4, lastlon);
        put(record + 8, a);
        put(record + 12, b);
        if (fwrite(record, 1, 16, out) != 16)
          goto io_error;
      }
      lastlat = a;
      lastlon = b;
      have = true;
    }
  }
  if (ferror(in))
    goto io_error;
  if (inside || !osm || !closed || !m.segments)
    goto invalid;
  header(h, &m, 0);
  if (fseek(out, 0, SEEK_SET) || fwrite(h, 1, HEADER, out) != HEADER ||
      fflush(out) || fsync(fileno(out)))
    goto io_error;
  if (fclose(out)) {
    out = NULL;
    goto io_error;
  }
  out = NULL;
  /* CRC covers header and geometry; sync and verify again before publication.
   */
  uint32_t crc;
  if (pos_fs_crc_file(part, HEADER + m.segments * 16, &crc))
    goto io_error;
  out = fopen(part, "r+b");
  if (!out)
    goto io_error;
  put(h, crc);
  if (fseek(out, 20, SEEK_SET) || fwrite(h, 1, 4, out) != 4 || fflush(out) ||
      fsync(fileno(out)))
    goto io_error;
  if (fclose(out)) {
    out = NULL;
    goto io_error;
  }
  out = NULL;
  uint8_t *pixels = calloc(1, POS_MAP_PIXELS);
  if (!pixels) {
    fail(e, "Not enough memory to verify map.");
    goto done;
  }
  result = pos_map_raster(part, m.latitude, m.longitude, m.width_m, pixels,
                          progress, ctx, e);
  free(pixels);
  if (result)
    goto done;
  if (pos_fs_publish_new(part, destination)) {
    result = -1;
    goto io_error;
  }
  goto done;
invalid:
  fail(e,
       "No usable roads or incomplete OSM response. Try a smaller area later.");
  goto done;
io_error:
  result = -1;
  fail(e, "Map SD operation failed; existing maps preserved.");
done:
  if (out)
    fclose(out);
  if (in)
    fclose(in);
  free(buffers);
  /* Incomplete .part stays excluded from the map library. */
  return result;
}
void pos_map_project(double lat, double lon, double clat, double clon,
                     unsigned width, int *x, int *y) {
  double px =
      (lon - clon) * METERS * cos(clat * RAD) * POS_MAP_WIDTH / width + 120;
  double py =
      (clat - lat) * METERS * POS_MAP_WIDTH / width + POS_MAP_HEIGHT / 2;
  *x = (int)fmax(-32760, fmin(32760, px));
  *y = (int)fmax(-32760, fmin(32760, py));
}
static unsigned code(int x, int y) {
  return (x < 0 ? 1 : 0) | (x >= 240 ? 2 : 0) | (y < 0 ? 4 : 0) |
         (y >= POS_MAP_HEIGHT ? 8 : 0);
}
void pos_map_line(uint8_t pixels[POS_MAP_PIXELS], int x, int y, int xx,
                  int yy) {
  /* Clip first: a long rural road must not cause millions of pixel steps. */
  for (unsigned n = 0; n < 8; ++n) {
    unsigned a = code(x, y), b = code(xx, yy), o = a ? a : b;
    if (!(a | b))
      break;
    if (a & b)
      return;
    double nx = x, ny = y;
    if (o & 8) {
      ny = POS_MAP_HEIGHT - 1;
      nx = x + (double)(xx - x) * (ny - y) / (yy - y);
    } else if (o & 4) {
      ny = 0;
      nx = x + (double)(xx - x) * (ny - y) / (yy - y);
    } else if (o & 2) {
      nx = 239;
      ny = y + (double)(yy - y) * (nx - x) / (xx - x);
    } else {
      nx = 0;
      ny = y + (double)(yy - y) * (nx - x) / (xx - x);
    }
    if (a) {
      x = (int)nx;
      y = (int)ny;
    } else {
      xx = (int)nx;
      yy = (int)ny;
    }
  }
  if (code(x, y) | code(xx, yy))
    return;
  int dx = abs(xx - x), sx = x < xx ? 1 : -1, dy = -abs(yy - y),
      sy = y < yy ? 1 : -1, err = dx + dy;
  for (;;) {
    unsigned p = (unsigned)(y * 240 + x);
    pixels[p / 8] |= (uint8_t)(1u << (p % 8));
    if (x == xx && y == yy)
      break;
    int twice = 2 * err;
    if (twice >= dy) {
      err += dy;
      x += sx;
    }
    if (twice <= dx) {
      err += dx;
      y += sy;
    }
  }
}
int pos_map_raster(const char *path, double lat, double lon, unsigned width,
                   uint8_t pixels[POS_MAP_PIXELS], pos_io_progress_t progress,
                   void *ctx, pos_error_t *e) {
  pos_map_info_t m;
  uint8_t h[HEADER];
  if (!pixels || !isfinite(lat) || !isfinite(lon) || fabs(lat) > 80 ||
      fabs(lon) > 180 || width < 100 || width > 16000 ||
      pos_map_inspect(path, &m, e))
    return fail(e, "Invalid map or viewport.");
  FILE *f = fopen(path, "rb");
  int result = -1;
  if (!f || read_header(f, &m, h))
    goto done;
  uint32_t expected = get(h + 20);
  memset(h + 20, 0, 4);
  uint32_t crc = pos_crc32(0, h, HEADER);
  memset(pixels, 0, POS_MAP_PIXELS);
  for (unsigned i = 0; i < m.segments; ++i) {
    uint8_t record[16];
    if (fread(record, 1, 16, f) != 16)
      goto done;
    crc = pos_crc32(crc, record, 16);
    double a = (int32_t)get(record) / 1000000.,
           b = (int32_t)get(record + 4) / 1000000.,
           c = (int32_t)get(record + 8) / 1000000.,
           d = (int32_t)get(record + 12) / 1000000.;
    if (fabs(a) > 90 || fabs(c) > 90 || fabs(b) > 180 || fabs(d) > 180)
      goto done;
    int x, y, xx, yy;
    pos_map_project(a, b, lat, lon, width, &x, &y);
    pos_map_project(c, d, lat, lon, width, &xx, &yy);
    pos_map_line(pixels, x, y, xx, yy);
    if (!(i % 256) && progress && progress(ctx, "Drawing map", i, m.segments))
      goto done;
  }
  if (ferror(f) || crc != expected)
    goto done;
  result = 0;
done:
  if (f)
    fclose(f);
  if (result) {
    memset(pixels, 0, POS_MAP_PIXELS);
    return fail(e,
                "Map damaged or operation cancelled. Existing files retained.");
  }
  return 0;
}
double pos_map_bearing(double lat, double lon, double dest_lat,
                       double dest_lon) {
  double a = lat * RAD, b = dest_lat * RAD, d = (dest_lon - lon) * RAD;
  double result =
      atan2(sin(d) * cos(b), cos(a) * sin(b) - sin(a) * cos(b) * cos(d)) / RAD;
  return fmod(result + 360, 360);
}

static double world_delta(double lon) {
  return lon - 360. * floor((lon + 180.) / 360.);
}
static double world_scale(double lat, unsigned zoom) {
  return (86. / (180. + 2. * fabs(lat))) * (1u << zoom);
}
void pos_world_project(double lat, double lon, double clat, double clon,
                       unsigned zoom, int *x, int *y) {
  if (!isfinite(lat) || !isfinite(lon) || !isfinite(clat) || !isfinite(clon) ||
      fabs(clon) > 180 || fabs(lon) > 180 || fabs(clat) > 90 ||
      fabs(lat) > 90 || zoom > 8) {
    *x = *y = -32760;
    return;
  }
  double scale = world_scale(clat, zoom);
  *x = (int)lround(120 + world_delta(lon - clon) * scale);
  *y = (int)lround(44 + (clat - lat) * scale);
}
int pos_world_raster(const char *path, double lat, double lon, unsigned zoom,
                     uint8_t pixels[POS_MAP_PIXELS], pos_error_t *e) {
  if (!pixels)
    return fail(e, "No world-map buffer.");
  memset(pixels, 0, POS_MAP_PIXELS);
  if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180 ||
      zoom > 8)
    return fail(e, "Invalid world-map viewport.");
  FILE *f = fopen(path, "rb");
  uint8_t h[12], records[512];
  int result = -1;
  if (!f || fread(h, 1, 12, f) != 12 || memcmp(h, "PWM1", 4))
    goto done;
  uint32_t count = get(h + 4), expected = get(h + 8), crc = 0;
  if (!count || count > 20000)
    goto done;
  double scale = world_scale(lat, zoom);
  for (unsigned first = 0; first < count;) {
    unsigned n = count - first;
    if (n > 64)
      n = 64;
    if (fread(records, 8, n, f) != n)
      goto done;
    crc = pos_crc32(crc, records, n * 8);
    for (unsigned i = 0; i < n; ++i) {
      int coord[4];
      for (unsigned j = 0; j < 4; ++j) {
        unsigned k = i * 8 + j * 2;
        unsigned value = records[k] | (unsigned)records[k + 1] << 8;
        coord[j] = value >= 32768 ? (int)value - 65536 : (int)value;
        if (abs(coord[j]) > (j % 2 ? 9000 : 18000))
          goto done;
      }
      double a = world_delta(coord[0] / 100. - lon);
      double b = a + world_delta((coord[2] - coord[0]) / 100.);
      /* Draw the short segment on both sides when it crosses the camera seam;
       * never connect the two date-line edges through the map interior. */
      for (int copy = -1; copy <= 1; ++copy) {
        double aa = a + copy * 360, bb = b + copy * 360;
        if (fmin(aa, bb) > 180 || fmax(aa, bb) < -180)
          continue;
        double t0 = 0, t1 = 1, dx = bb - aa;
        if (dx) {
          double u = (-180 - aa) / dx, v = (180 - aa) / dx;
          t0 = fmax(0, fmin(u, v));
          t1 = fmin(1, fmax(u, v));
        }
        if (t0 > t1)
          continue;
        double y0 = coord[1] / 100., dy = (coord[3] - coord[1]) / 100.;
        int x = (int)lround(120 + (aa + dx * t0) * scale);
        int xx = (int)lround(120 + (aa + dx * t1) * scale);
        int y = (int)lround(44 + (lat - y0 - dy * t0) * scale);
        int yy = (int)lround(44 + (lat - y0 - dy * t1) * scale);
        pos_map_line(pixels, x, y, xx, yy);
      }
    }
    first += n;
  }
  if (crc != expected || fgetc(f) != EOF || ferror(f))
    goto done;
  result = 0;
done:
  if (f)
    fclose(f);
  if (result) {
    memset(pixels, 0, POS_MAP_PIXELS);
    return fail(e, "World map missing or damaged. Reinstall system assets; "
                   "regional maps remain available.");
  }
  return 0;
}
