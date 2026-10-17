#include "pos_navigation.h"
#include "pos_location.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define NH 64u
#define NODE 24u
#define EDGE 20u
#define WAY 80u
#define RULE 32u
#define POINT 80u
#define LIMIT 60000u
#define RUN 256u
#define PI 3.14159265358979323846
#define INF UINT32_MAX
static uint32_t u32(const void *data) {
  const uint8_t *p = data;
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static uint64_t u64(const void *data) {
  const uint8_t *p = data;
  return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32;
}
static void w32(void *data, uint32_t n) {
  uint8_t *p = data;
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(n >> (8 * i));
}
static void w64(void *p, uint64_t n) {
  w32(p, (uint32_t)n);
  w32((uint8_t *)p + 4, (uint32_t)(n >> 32));
}
static int error(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static int tick(pos_io_progress_t p, void *c, const char *s, uint64_t a,
                uint64_t b) {
  return p && p(c, s, a, b);
}
static FILE *file(const char *p, const char *mode) {
  FILE *f = fopen(p, mode);
  if (f)
    setvbuf(f, NULL, _IOFBF, 4096);
  return f;
}
static int sync_close(FILE **f) {
  int bad = fflush(*f) || fsync(fileno(*f));
  if (fclose(*f))
    bad = 1;
  *f = NULL;
  return bad ? -1 : 0;
}
static int at(FILE *f, uint32_t offset, void *p, unsigned n) {
  return fseek(f, (long)offset, SEEK_SET) || fread(p, 1, n, f) != n ? -1 : 0;
}
static uint32_t crc_header(uint8_t *h, unsigned at_crc) {
  uint32_t expected = u32(h + at_crc);
  memset(h + at_crc, 0, 4);
  return expected;
}
static int checksum(FILE *f, uint8_t h[NH], unsigned position, uint32_t size,
                    pos_io_progress_t p, void *ctx) {
  uint32_t expected = crc_header(h, position), crc = pos_crc32(0, h, NH),
           done = NH;
  uint8_t *b = malloc(4096);
  if (!b)
    return -1;
  if (fseek(f, NH, SEEK_SET)) {
    free(b);
    return -1;
  }
  while (done < size) {
    unsigned n = size - done < 4096 ? size - done : 4096;
    if (fread(b, 1, n, f) != n) {
      free(b);
      return -1;
    }
    crc = pos_crc32(crc, b, n);
    done += n;
    if (tick(p, ctx, "Checking navigation", done, size)) {
      free(b);
      return -1;
    }
  }
  free(b);
  return crc == expected ? 0 : -1;
}
static int seal(FILE **f, const char *path, uint8_t h[NH], unsigned position,
                uint32_t size) {
  if (fseek(*f, 0, SEEK_SET) || fwrite(h, 1, NH, *f) != NH || sync_close(f))
    return -1;
  uint32_t crc;
  if (pos_fs_crc_file(path, size, &crc))
    return -1;
  *f = file(path, "r+b");
  if (!*f)
    return -1;
  w32(h + position, crc);
  if (fseek(*f, position, SEEK_SET) || fwrite(h + position, 1, 4, *f) != 4)
    return -1;
  return sync_close(f);
}
/* Bounded token reader. Geometry/restriction tags may never be truncated;
 * long non-routing descriptions are skipped. Quotes protect embedded '>'. */
static int token(FILE *f, char out[768], uint32_t *bytes) {
  int c;
  while ((c = fgetc(f)) != EOF) {
    if (++*bytes > POS_MAP_DOWNLOAD_LIMIT)
      return -1;
    if (c == '<')
      break;
  }
  if (c == EOF)
    return ferror(f) ? -1 : 0;
  unsigned n = 0;
  int quote = 0;
  bool overflow = false;
  while ((c = fgetc(f)) != EOF) {
    if (++*bytes > POS_MAP_DOWNLOAD_LIMIT)
      return -1;
    if (!quote && c == '>') {
      out[n] = 0;
      return overflow ? -2 : 1;
    }
    if (c == '\'' || c == '"') {
      if (quote == c)
        quote = 0;
      else if (!quote)
        quote = c;
    }
    if (n < 767)
      out[n++] = (char)c;
    else
      overflow = true;
  }
  return -1;
}
static int attr(const char *t, const char *key, char *out, unsigned cap) {
  while (*t && !isspace((unsigned char)*t))
    ++t;
  while (*t) {
    while (isspace((unsigned char)*t))
      ++t;
    const char *name = t;
    while (*t && (isalnum((unsigned char)*t) || strchr("_:-", *t)))
      ++t;
    unsigned n = (unsigned)(t - name);
    if (!n)
      break;
    while (isspace((unsigned char)*t))
      ++t;
    if (*t++ != '=')
      return -1;
    while (isspace((unsigned char)*t))
      ++t;
    int q = *t++;
    if (q != '"' && q != '\'')
      return -1;
    const char *value = t;
    while (*t && *t != q)
      ++t;
    if (!*t)
      return -1;
    if (n == strlen(key) && !memcmp(name, key, n)) {
      unsigned used = (unsigned)(t - value);
      if (used >= cap)
        return -1;
      memcpy(out, value, used);
      out[used] = 0;
      return 0;
    }
    ++t;
  }
  return -1;
}
static int ident(const char *tag, const char *key, uint64_t *id) {
  char value[24], *end;
  if (attr(tag, key, value, sizeof(value)) || !*value ||
      strspn(value, "0123456789") != strlen(value))
    return -1;
  errno = 0;
  unsigned long long n = strtoull(value, &end, 10);
  if (errno || *end || !n)
    return -1;
  *id = n;
  return 0;
}
static int coord(const char *tag, const char *key, int32_t *out) {
  char value[32], *end;
  double n;
  if (attr(tag, key, value, sizeof(value)))
    return -1;
  n = strtod(value, &end);
  if (end == value || *end || !isfinite(n) ||
      fabs(n) > (!strcmp(key, "lat") ? 90 : 180))
    return -1;
  *out = (int32_t)llround(n * 1000000);
  return 0;
}
static void name_decode(char *out, unsigned cap, const char *text) {
  unsigned n = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p && n + 1 < cap;
       ++p) {
    if (*p == '&') {
      const struct {
        const char *entity;
        char value;
      } entities[] = {{"amp;", '&'},
                      {"quot;", '"'},
                      {"apos;", '\''},
                      {"lt;", '<'},
                      {"gt;", '>'}};
      bool decoded = false;
      for (unsigned i = 0; i < 5; ++i)
        if (!strncmp((const char *)p + 1, entities[i].entity,
                     strlen(entities[i].entity))) {
          out[n++] = entities[i].value;
          p += strlen(entities[i].entity);
          decoded = true;
          break;
        }
      if (decoded)
        continue;
    }
    if (*p >= 32 && *p < 127)
      out[n++] = (char)*p;
    else if ((*p & 0xc0) != 0x80)
      out[n++] = '?';
  }
  out[n] = 0;
}
static int compare64(const void *a, const void *b) {
  uint64_t x = u64(a), y = u64(b);
  return x < y ? -1 : x > y;
}
static int compare32(const void *a, const void *b) {
  uint32_t x = u32(a), y = u32(b);
  return x < y ? -1 : x > y;
}
static int path(char out[POS_FS_PATH], const char *folder, const char *stem,
                unsigned pass, unsigned run) {
  return snprintf(out, POS_FS_PATH, "%s/%s-%u-%u", folder, stem, pass, run) >=
                 POS_FS_PATH
             ? -1
             : 0;
}
static int sort_file(const char *input, const char *output, const char *folder,
                     const char *stem, unsigned size, bool wide,
                     pos_io_progress_t progress, void *ctx) {
  FILE *in = file(input, "rb");
  uint8_t *batch = malloc(RUN * size);
  unsigned runs = 0, pass = 0;
  int result = -1;
  char a[POS_FS_PATH], b[POS_FS_PATH], o[POS_FS_PATH];
  if (!in || !batch)
    goto done;
  int (*cmp)(const void *, const void *) = wide ? compare64 : compare32;
  size_t n;
  while ((n = fread(batch, size, RUN, in))) {
    qsort(batch, n, size, cmp);
    if (path(o, folder, stem, 0, runs++))
      goto done;
    FILE *f = file(o, "wbx");
    if (!f)
      goto done;
    int bad = fwrite(batch, size, n, f) != n;
    if (sync_close(&f) || bad)
      goto done;
    if (tick(progress, ctx, "Sorting map graph", runs, 0))
      goto done;
  }
  if (ferror(in))
    goto done;
  fclose(in);
  in = NULL;
  if (!runs) {
    FILE *f = file(output, "wbx");
    if (f) {
      result = sync_close(&f);
    }
    goto done;
  }
  while (runs > 1) {
    unsigned next = 0;
    for (unsigned r = 0; r < runs; r += 2) {
      if (path(a, folder, stem, pass, r) ||
          path(b, folder, stem, pass, r + 1) ||
          path(o, folder, stem, pass + 1, next++))
        goto done;
      if (r + 1 == runs) {
        if (rename(a, o))
          goto done;
        continue;
      }
      FILE *left = file(a, "rb"), *right = file(b, "rb"), *out = file(o, "wbx");
      uint8_t x[32], y[32];
      int bad = !left || !right || !out;
      size_t nx = left ? fread(x, size, 1, left) : 0,
             ny = right ? fread(y, size, 1, right) : 0;
      unsigned count = 0;
      while (!bad && (nx || ny)) {
        bool first = nx && (!ny || cmp(x, y) <= 0);
        if (fwrite(first ? x : y, size, 1, out) != 1) {
          bad = 1;
          break;
        }
        if (first)
          nx = fread(x, size, 1, left);
        else
          ny = fread(y, size, 1, right);
        if (!(++count % 1024) &&
            tick(progress, ctx, "Merging map graph", count, 0))
          bad = 1;
      }
      if (left) {
        if (ferror(left))
          bad = 1;
        fclose(left);
      }
      if (right) {
        if (ferror(right))
          bad = 1;
        fclose(right);
      }
      if (out && sync_close(&out))
        bad = 1;
      if (bad)
        goto done;
      unlink(a);
      unlink(b);
    }
    runs = next;
    ++pass;
  }
  if (path(a, folder, stem, pass, 0) || rename(a, output))
    goto done;
  result = 0;
done:
  if (in)
    fclose(in);
  free(batch);
  return result;
}
static void cleanup(const char *folder) {
  DIR *d = opendir(folder);
  if (!d)
    return;
  struct dirent *e;
  char p[POS_FS_PATH];
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.')
      continue;
    if (snprintf(p, sizeof(p), "%s/%s", folder, e->d_name) < (int)sizeof(p))
      unlink(p);
  }
  closedir(d);
  rmdir(folder);
}
static int workspace(const char *target, char out[POS_FS_PATH]) {
  for (unsigned i = 0; i < 100; ++i) {
    if (snprintf(out, POS_FS_PATH, "%s.work-%u", target, i) >= POS_FS_PATH)
      break;
    if (!mkdir(out, 0700))
      return 0;
    if (errno != EEXIST)
      break;
  }
  out[0] = 0;
  return -1;
}
static int simple_path(char out[POS_FS_PATH], const char *dir,
                       const char *name) {
  return snprintf(out, POS_FS_PATH, "%s/%s", dir, name) >= POS_FS_PATH ? -1 : 0;
}
/* Missing/conditional access is conservative. Explicit mode tags override
 * general access, independently of tag ordering in the XML. */
static int allowed(const char *v) {
  return !strcmp(v, "yes") || !strcmp(v, "designated") ||
         !strcmp(v, "permissive") || !strcmp(v, "official");
}
static unsigned access_mask(const char values[6][40], unsigned base) {
  const unsigned affected[] = {7, 6, 4, 4, 1, 2};
  unsigned defaults = base;
  for (unsigned i = 0; i < 6; ++i)
    if (*values[i]) {
      if (allowed(values[i]))
        base |= i ? affected[i] : defaults;
      else
        base &= ~affected[i];
    }
  return base;
}
static int access_index(const char *key) {
  const char *keys[] = {"access",   "vehicle", "motor_vehicle",
                        "motorcar", "foot",    "bicycle"};
  for (unsigned i = 0; i < 6; ++i)
    if (!strcmp(key, keys[i]))
      return (int)i;
  return -1;
}
static unsigned base_modes(const char *highway) {
  char key[64];
  snprintf(key, sizeof(key), "|%s|", highway);
  if (*highway &&
      strstr(
          "|residential|living_street|service|unclassified|tertiary|tertiary_"
          "link|secondary|secondary_link|primary|primary_link|track|",
          key))
    return 7;
  if (!strcmp(highway, "motorway") || !strcmp(highway, "motorway_link") ||
      !strcmp(highway, "trunk") || !strcmp(highway, "trunk_link"))
    return 4;
  if (!strcmp(highway, "path"))
    return 3;
  if (!strcmp(highway, "footway") || !strcmp(highway, "pedestrian") ||
      !strcmp(highway, "steps") || !strcmp(highway, "bridleway"))
    return 1;
  if (!strcmp(highway, "cycleway"))
    return 2;
  return 0;
}
static int direction(const char *s) {
  if (!strcmp(s, "yes") || !strcmp(s, "1") || !strcmp(s, "true"))
    return 1;
  if (!strcmp(s, "-1"))
    return -1;
  if (!*s || !strcmp(s, "no") || !strcmp(s, "0") || !strcmp(s, "false"))
    return 0;
  return 2;
}
typedef struct {
  char dir[POS_FS_PATH], names[8][POS_FS_PATH], tag[768];
  FILE *xml, *nodes, *segments, *ways, *rules, *barriers, *geometry;
  unsigned nw, nr, points, segments_count;
} compiler_t;
static int emit_way(compiler_t *c, uint64_t id, const char *name,
                    unsigned masks, unsigned points) {
  uint8_t record[WAY] = {0}, a[16], b[16];
  w64(record, id);
  memcpy(record + 8, name, strlen(name));
  rewind(c->geometry);
  if (points && fread(a, 1, 16, c->geometry) != 16)
    return -1;
  if (points) {
    memcpy(record + 72, a + 8, 8);
  }
  if (fwrite(record, 1, WAY, c->ways) != WAY)
    return -1;
  for (unsigned i = 1; i < points; ++i) {
    if (fread(b, 1, 16, c->geometry) != 16)
      return -1;
    double meters = pos_location_distance(
        (int32_t)u32(a + 8) / 1e6, (int32_t)u32(a + 12) / 1e6,
        (int32_t)u32(b + 8) / 1e6, (int32_t)u32(b + 12) / 1e6);
    if (meters > 20000000 || !isfinite(meters))
      return -1;
    if (u64(a) != u64(b) && masks) {
      uint8_t edge[32] = {0};
      memcpy(edge, a, 8);
      memcpy(edge + 8, b, 8);
      w32(edge + 16, c->nw);
      w32(edge + 20, (uint32_t)fmax(1, ceil(meters * 100)));
      w32(edge + 24, masks);
      if (fwrite(edge, 1, 32, c->segments) != 32)
        return -1;
    }
    memcpy(a, b, 16);
  }
  ++c->nw;
  rewind(c->geometry);
  return c->nw > POS_MAP_SEGMENTS ? -1 : 0;
}
static unsigned condition_mask(const char *key) {
  const char *suffix = strstr(key, ":conditional");
  if (!suffix)
    return 0;
  if (!strncmp(key, "foot", 4) || !strncmp(key, "oneway:foot", 11))
    return 1;
  if (!strncmp(key, "bicycle", 7) || !strncmp(key, "oneway:bicycle", 14))
    return 2;
  if (!strncmp(key, "motor", 5))
    return 4;
  if (!strncmp(key, "vehicle", 7) || !strncmp(key, "oneway", 6))
    return 6;
  return !strncmp(key, "access", 6) ? 7 : 0;
}
static bool excepts(const char *text, const char *key) {
  unsigned k = strlen(key);
  while (*text) {
    while (*text == ' ' || *text == ';')
      ++text;
    const char *end = strchr(text, ';');
    unsigned n = end ? (unsigned)(end - text) : strlen(text);
    while (n && text[n - 1] == ' ')
      --n;
    if (n == k && !memcmp(text, key, n))
      return true;
    if (!end)
      break;
    text = end + 1;
  }
  return false;
}
static bool routing_key(const char *key, bool relation) {
  if (relation)
    return !strncmp(key, "restriction", 11) || !strcmp(key, "except");
  const char *names[] = {"name",          "highway",  "barrier", "junction",
                         "cycleway",      "area",     "access",  "vehicle",
                         "motor_vehicle", "motorcar", "foot",    "bicycle",
                         "oneway"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    size_t n = strlen(names[i]);
    if (!strncmp(key, names[i], n) && (!key[n] || key[n] == ':'))
      return true;
  }
  return false;
}
typedef struct {
  char dir[POS_FS_PATH], raw[POS_FS_PATH], sorted[POS_FS_PATH];
  char staged[POS_FS_PATH], object[POS_FS_PATH], tag[768];
  FILE *input, *nodes, *output, *spool;
} import_t;
/* Binary searches hit the SD index, not an unbounded RAM node dictionary. */
static int import_node(FILE *f, unsigned count, uint64_t id, uint8_t out[16]) {
  unsigned low = 0, high = count;
  while (low < high) {
    unsigned middle = low + (high - low) / 2;
    if (at(f, middle * 16, out, 16))
      return -1;
    uint64_t found = u64(out);
    if (found == id)
      return 0;
    if (found < id)
      low = middle + 1;
    else
      high = middle;
  }
  return -1;
}
static int import_copy(FILE *in, FILE *out) {
  uint8_t buffer[512];
  rewind(in);
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), in)))
    if (fwrite(buffer, 1, n, out) != n)
      return -1;
  return ferror(in) ? -1 : 0;
}
int pos_nav_import_osm(const char *source, const char *destination,
                       pos_io_progress_t progress, void *ctx, pos_error_t *e) {
  import_t *s = calloc(1, sizeof(*s));
  int result = -1, status;
  unsigned count = 0, points = 0;
  uint32_t size, bytes = 0, last = 0;
  const char *why =
      "OSM preparation failed: invalid/missing node data, SD space "
      "or memory. Existing maps and source preserved.";
  if (!s || pos_fs_file_size(source, &size) || !size ||
      size > POS_MAP_DOWNLOAD_LIMIT || !access(destination, F_OK) ||
      workspace(destination, s->dir))
    goto done;
  if (simple_path(s->raw, s->dir, "nodes.raw") ||
      simple_path(s->sorted, s->dir, "nodes.sorted") ||
      simple_path(s->staged, s->dir, "roads.osm") ||
      simple_path(s->object, s->dir, "object"))
    goto done;
  s->input = file(source, "rb");
  s->nodes = file(s->raw, "wbx");
  if (!s->input || !s->nodes)
    goto done;
  /* First pass accepts arbitrary node ordering and 64-bit OSM identifiers. */
  while ((status = token(s->input, s->tag, &bytes)) > 0 || status == -2) {
    if (!strncmp(s->tag, "node ", 5)) {
      uint64_t id;
      int32_t lat, lon;
      uint8_t b[16];
      if (status == -2 || ident(s->tag, "id", &id) ||
          coord(s->tag, "lat", &lat) || coord(s->tag, "lon", &lon) ||
          ++count > LIMIT)
        goto done;
      w64(b, id);
      w32(b + 8, (uint32_t)lat);
      w32(b + 12, (uint32_t)lon);
      if (fwrite(b, 1, 16, s->nodes) != 16)
        goto done;
    }
    if (bytes - last >= 4096) {
      last = bytes;
      if (tick(progress, ctx, "Indexing OSM nodes", bytes, size)) {
        why = "Map preparation cancelled; original data preserved.";
        goto done;
      }
    }
  }
  if (status < 0 || sync_close(&s->nodes) ||
      sort_file(s->raw, s->sorted, s->dir, "import", 16, true, progress, ctx))
    goto done;
  s->nodes = file(s->sorted, "rb");
  if (!s->nodes)
    goto done;
  uint8_t b[16];
  uint64_t previous = 0;
  for (unsigned i = 0; i < count; ++i) {
    if (fread(b, 1, 16, s->nodes) != 16 || u64(b) <= previous)
      goto done;
    previous = u64(b);
  }
  rewind(s->input);
  bytes = last = 0;
  s->output = file(s->staged, "wbx");
  if (!s->output || fputs("<osm version=\"0.6\">\n", s->output) == EOF)
    goto done;
  bool osm = false, closed = false, keep = false;
  unsigned object = 0, roads = 0; /* 1=node, 2=way, 3=relation */
  while ((status = token(s->input, s->tag, &bytes)) > 0 || status == -2) {
    char *t = s->tag;
    char key[80], value[80];
    if (closed)
      goto done;
    if (bytes - last >= 4096) {
      last = bytes;
      if (ftell(s->output) > POS_MAP_DOWNLOAD_LIMIT ||
          (s->spool && ftell(s->spool) > POS_MAP_DOWNLOAD_LIMIT))
        goto done;
      if (tick(progress, ctx, "Resolving OSM roads", bytes, size)) {
        why = "Map preparation cancelled; original data preserved.";
        goto done;
      }
    }
    if (status == -2) {
      if (!strncmp(t, "tag ", 4) && !attr(t, "k", key, sizeof(key)) &&
          strcmp(key, "type") && !routing_key(key, object == 3))
        continue;
      goto done;
    }
    if (!strncmp(t, "osm ", 4)) {
      if (osm || object)
        goto done;
      osm = true;
      continue;
    }
    if (!strcmp(t, "/osm")) {
      if (!osm || object)
        goto done;
      closed = true;
      int c;
      while ((c = fgetc(s->input)) != EOF)
        if (!isspace((unsigned char)c))
          goto done;
      if (ferror(s->input))
        goto done;
      status = 0;
      break;
    }
    if (!strncmp(t, "!DOCTYPE", 8) || !strncmp(t, "remark", 6) ||
        !strncmp(t, "error", 5))
      goto done;
    unsigned opening = !strncmp(t, "node ", 5)       ? 1
                       : !strncmp(t, "way ", 4)      ? 2
                       : !strncmp(t, "relation ", 9) ? 3
                                                     : 0;
    if (opening) {
      uint64_t id;
      if (!osm || object || ident(t, "id", &id))
        goto done;
      object = opening;
      keep = object == 1;
      /* Coordinate-only nodes already live in the index. Avoid creating tens
       * of thousands of tiny SD spool files for nodes with no routing tags. */
      if (object == 1 && t[strlen(t) - 1] == '/') {
        object = 0;
        continue;
      }
      if (object != 1) {
        s->spool = file(s->object, "w+bx");
        if (!s->spool)
          goto done;
      }
    }
    if (!strncmp(t, "nd ", 3)) {
      uint64_t id;
      int32_t lat, lon;
      if (object != 2 || ++points > LIMIT || ident(t, "ref", &id))
        goto done;
      if (coord(t, "lat", &lat) || coord(t, "lon", &lon)) {
        if (import_node(s->nodes, count, id, b))
          goto done;
        lat = (int32_t)u32(b + 8);
        lon = (int32_t)u32(b + 12);
      }
      if (fprintf(s->spool, "<nd ref=\"%llu\" lat=\"%.6f\" lon=\"%.6f\"/>\n",
                  (unsigned long long)id, lat / 1e6, lon / 1e6) < 0)
        goto done;
    } else if (object) {
      if (!strncmp(t, "tag ", 4) && !attr(t, "k", key, sizeof(key))) {
        if ((object == 2 && !strcmp(key, "highway")) ||
            (object == 3 && !strcmp(key, "type") &&
             !attr(t, "v", value, sizeof(value)) &&
             !strcmp(value, "restriction")))
          keep = true;
      }
      if (fprintf(object == 1 ? s->output : s->spool, "<%s>\n", t) < 0)
        goto done;
    }
    bool end =
        !strcmp(t, "/node") || !strcmp(t, "/way") || !strcmp(t, "/relation");
    if (end && ((!strcmp(t, "/node") && object != 1) ||
                (!strcmp(t, "/way") && object != 2) ||
                (!strcmp(t, "/relation") && object != 3)))
      goto done;
    if (end || (opening && t[strlen(t) - 1] == '/')) {
      if (object != 1 && keep && import_copy(s->spool, s->output))
        goto done;
      if (keep && object == 2)
        ++roads;
      if (s->spool) {
        if (fclose(s->spool)) {
          s->spool = NULL;
          goto done;
        }
        s->spool = NULL;
        if (unlink(s->object))
          goto done;
      }
      object = 0;
    }
  }
  if (status < 0 || !closed || !roads || object ||
      fputs("</osm>\n", s->output) == EOF || sync_close(&s->output) ||
      pos_fs_file_size(s->staged, &size) || size > POS_MAP_DOWNLOAD_LIMIT ||
      !access(destination, F_OK) || rename(s->staged, destination))
    goto done;
  result = 0;
done:
  if (s) {
    FILE *files[] = {s->input, s->nodes, s->output, s->spool};
    for (unsigned i = 0; i < 4; ++i)
      if (files[i])
        fclose(files[i]);
    if (*s->dir)
      cleanup(s->dir);
    free(s);
  }
  return result ? error(e, why) : 0;
}
static int parse(compiler_t *c, pos_io_progress_t progress, void *ctx) {
  bool osm = false, closed = false, inway = false, inrel = false,
       innode = false;
  uint32_t bytes = 0, last = 0;
  int status;
  uint64_t id = 0, from = 0, to = 0, via = 0;
  unsigned point_count = 0, blocked = 0, ruleblocked = 0;
  char directional[2][6][40] = {{{0}}};
  char accesses[6][40] = {{0}}, highway[40] = "", oneway[16] = "",
       owbike[16] = "", owfoot[16] = "", owvehicle[16] = "", owmotor[16] = "",
       owcar[16] = "", junction[24] = "", cycleway[32] = "", name[64] = "",
       barrier[32] = "", rules[6][48] = {{0}}, except[80] = "";
  bool area = false, viaway = false;
  while ((status = token(c->xml, c->tag, &bytes)) > 0 || status == -2) {
    if (bytes - last >= 4096) {
      last = bytes;
      if (tick(progress, ctx, "Building road graph", bytes, 0))
        return -1;
    }
    char *t = c->tag;
    if (closed)
      return -1;
    if (status == -2) {
      char key[80];
      if (!strncmp(t, "tag ", 4) && !attr(t, "k", key, sizeof(key)) &&
          !routing_key(key, inrel))
        continue;
      return -1;
    }
    if (!strncmp(t, "osm ", 4)) {
      if (osm)
        return -1;
      osm = true;
    } else if (!strcmp(t, "/osm")) {
      if (!osm || inway || inrel || innode)
        return -1;
      closed = true;
    } else if (!strncmp(t, "!DOCTYPE", 8) || !strncmp(t, "remark", 6) ||
               !strncmp(t, "error", 5))
      return -1;
    else if (!strncmp(t, "way ", 4) || !strncmp(t, "node ", 5) ||
             !strncmp(t, "relation ", 9)) {
      if (!osm || inway || inrel || innode || ident(t, "id", &id))
        return -1;
      inway = !strncmp(t, "way ", 4);
      innode = !strncmp(t, "node ", 5);
      inrel = !strncmp(t, "relation ", 9);
      memset(accesses, 0, sizeof(accesses));
      memset(directional, 0, sizeof(directional));
      *name = *highway = *oneway = *owbike = *owfoot = *junction = *cycleway =
          *barrier = *owvehicle = *owmotor = *owcar = 0;
      blocked = point_count = 0;
      area = false;
      if (inrel) {
        from = to = via = 0;
        viaway = false;
        ruleblocked = 0;
        memset(rules, 0, sizeof(rules));
        *except = 0;
      }
      if (t[strlen(t) - 1] == '/') {
        if (inway || inrel)
          return -1;
        innode = false;
      }
    } else if (!strncmp(t, "nd ", 3)) {
      if (!inway || ++c->points > LIMIT)
        return -1;
      uint8_t p[16];
      uint64_t node;
      int32_t lat, lon;
      if (ident(t, "ref", &node) || coord(t, "lat", &lat) ||
          coord(t, "lon", &lon))
        return -1;
      w64(p, node);
      w32(p + 8, (uint32_t)lat);
      w32(p + 12, (uint32_t)lon);
      if (fwrite(p, 1, 16, c->nodes) != 16 ||
          fwrite(p, 1, 16, c->geometry) != 16)
        return -1;
      ++point_count;
    } else if (!strncmp(t, "member ", 7) && inrel) {
      char role[24], type[16];
      uint64_t ref;
      if (attr(t, "role", role, sizeof(role)) ||
          attr(t, "type", type, sizeof(type)) || ident(t, "ref", &ref))
        return -1;
      if (!strcmp(role, "from")) {
        if (from || strcmp(type, "way"))
          return -1;
        from = ref;
      }
      if (!strcmp(role, "to")) {
        if (to || strcmp(type, "way"))
          return -1;
        to = ref;
      }
      if (!strcmp(role, "via")) {
        if (via) {
          viaway = true;
        } else {
          via = ref;
          viaway = strcmp(type, "node") != 0;
        }
      }
    } else if (!strncmp(t, "tag ", 4)) {
      char key[80], value[512];
      if (attr(t, "k", key, sizeof(key)))
        return -1;
      if (!routing_key(key, inrel))
        continue;
      if (attr(t, "v", value, sizeof(value)))
        return -1;
      if (inrel) {
        const char *keys[] = {"restriction",
                              "restriction:foot",
                              "restriction:bicycle",
                              "restriction:motorcar",
                              "restriction:motor_vehicle",
                              "restriction:vehicle"};
        for (unsigned i = 0; i < 6; ++i)
          if (!strcmp(key, keys[i])) {
            if (strlen(value) >= sizeof(rules[i]))
              return -1;
            strcpy(rules[i], value);
          }
        if (!strcmp(key, "except")) {
          if (strlen(value) >= sizeof(except))
            return -1;
          strcpy(except, value);
        }
        if (strstr(key, ":conditional"))
          ruleblocked |= strstr(key, ":foot")      ? 1
                         : strstr(key, ":bicycle") ? 2
                                                   : 6;
      } else if (inway || innode) {
        int ai = access_index(key);
        if (ai >= 0) {
          if (strlen(value) >= 40)
            return -1;
          strcpy(accesses[ai], value);
        }
        char access_key[80];
        snprintf(access_key, sizeof(access_key), "%s", key);
        char *scope = strrchr(access_key, ':');
        if (scope &&
            (!strcmp(scope, ":forward") || !strcmp(scope, ":backward"))) {
          unsigned dir = !strcmp(scope, ":backward");
          *scope = 0;
          int index = access_index(access_key);
          if (index >= 0) {
            if (strlen(value) >= 40)
              return -1;
            strcpy(directional[dir][index], value);
          }
        }
        blocked |= condition_mask(key);
        if (!strcmp(key, "name"))
          name_decode(name, sizeof(name), value);
        if (!strcmp(key, "highway")) {
          if (strlen(value) >= sizeof(highway))
            return -1;
          strcpy(highway, value);
        }
        if (!strcmp(key, "barrier")) {
          if (strlen(value) >= sizeof(barrier))
            return -1;
          strcpy(barrier, value);
        }
        if (!strcmp(key, "oneway")) {
          if (strlen(value) >= sizeof(oneway))
            return -1;
          strcpy(oneway, value);
        }
        if (!strcmp(key, "oneway:bicycle")) {
          if (strlen(value) >= sizeof(owbike))
            return -1;
          strcpy(owbike, value);
        }
        if (!strcmp(key, "oneway:foot")) {
          if (strlen(value) >= sizeof(owfoot))
            return -1;
          strcpy(owfoot, value);
        }
        if (!strcmp(key, "oneway:vehicle") ||
            !strcmp(key, "oneway:motor_vehicle") ||
            !strcmp(key, "oneway:motorcar")) {
          if (strlen(value) >= 16)
            return -1;
          strcpy(!strcmp(key, "oneway:vehicle")    ? owvehicle
                 : !strcmp(key, "oneway:motorcar") ? owcar
                                                   : owmotor,
                 value);
        }
        if (!strcmp(key, "junction")) {
          if (strlen(value) >= sizeof(junction))
            return -1;
          strcpy(junction, value);
        }
        if (!strcmp(key, "cycleway")) {
          if (strlen(value) >= sizeof(cycleway))
            return -1;
          strcpy(cycleway, value);
        }
        if (!strcmp(key, "area") && !strcmp(value, "yes"))
          area = true;
      }
    } else if (!strcmp(t, "/way")) {
      if (!inway || point_count < 2 ||
          c->segments_count + point_count - 1 > POS_MAP_SEGMENTS)
        return -1;
      c->segments_count += point_count - 1;
      unsigned modes =
          area ? 0 : access_mask(accesses, base_modes(highway)) & ~blocked;
      int d = direction(oneway);
      if (!*oneway && !strcmp(junction, "roundabout"))
        d = 1;
      int motor = *owcar       ? direction(owcar)
                  : *owmotor   ? direction(owmotor)
                  : *owvehicle ? direction(owvehicle)
                               : d;
      int dirs[] = {direction(owfoot),
                    *owbike ? direction(owbike)
                    : strstr(cycleway, "opposite") == cycleway ? 0
                    : *owvehicle ? direction(owvehicle)
                                 : d,
                    motor};
      unsigned forward_modes = access_mask(directional[0], modes) & ~blocked,
               backward_modes = access_mask(directional[1], modes) & ~blocked;
      unsigned mask = 0;
      for (unsigned i = 0; i < 3; ++i) {
        unsigned bit = 1u << i;
        if (dirs[i] != 2) {
          if ((forward_modes & bit) && dirs[i] >= 0)
            mask |= bit;
          if ((backward_modes & bit) && dirs[i] <= 0)
            mask |= bit << 3;
        }
      }
      if (emit_way(c, id, name, mask, point_count))
        return -1;
      inway = false;
    } else if (!strcmp(t, "/node")) {
      if (!innode)
        return -1;
      unsigned base = 7;
      if (*barrier)
        base =
            (!strcmp(barrier, "bollard") || !strcmp(barrier, "cycle_barrier"))
                ? 3
            : (!strcmp(barrier, "stile") || !strcmp(barrier, "kissing_gate"))
                ? 1
                : 0;
      if ((!strcmp(barrier, "gate") || !strcmp(barrier, "lift_gate") ||
           !strcmp(barrier, "swing_gate") || !strcmp(barrier, "chain")) &&
          allowed(accesses[0]))
        base = 7;
      unsigned mask = access_mask(accesses, base) & ~blocked;
      if (mask != 7) {
        uint8_t b[16] = {0};
        w64(b, id);
        w32(b + 8, mask);
        if (fwrite(b, 1, 16, c->barriers) != 16)
          return -1;
      }
      innode = false;
    } else if (!strcmp(t, "/relation")) {
      if (!inrel || !from || !to || !via)
        return -1;
      for (unsigned i = 0; i < 3; ++i) {
        unsigned mode = 1u << i;
        const char *r = *rules[i + 1]         ? rules[i + 1]
                        : i == 0              ? ""
                        : i == 2 && *rules[4] ? rules[4]
                        : *rules[5]           ? rules[5]
                                              : rules[0];
        if (excepts(except, i == 0   ? "foot"
                            : i == 1 ? "bicycle"
                                     : "motorcar") ||
            (i && excepts(except, "vehicle")) ||
            (i == 2 && excepts(except, "motor_vehicle")))
          continue;
        if (!*r && !(ruleblocked & mode))
          continue;
        unsigned kind = !strncmp(r, "only_", 5) ? 1 : 0;
        if (strstr(r, "u_turn"))
          kind |= 2;
        if (strstr(r, "straight_on"))
          kind |= 4;
        bool unknown = strncmp(r, "no_", 3) && strncmp(r, "only_", 5);
        if (viaway || unknown || (ruleblocked & mode))
          kind |= 8;
        uint8_t record[RULE] = {0};
        w64(record, from);
        w64(record + 8, to);
        w64(record + 16, kind & 8 ? 0 : via);
        w32(record + 24, mode);
        w32(record + 28, kind);
        if (++c->nr > 4096 || fwrite(record, 1, RULE, c->rules) != RULE)
          return -1;
      }
      inrel = false;
    }
  }
  return status == 0 && osm && closed && !inway && !inrel && !innode && c->nw
             ? 0
             : -1;
}
typedef struct {
  FILE *f;
  uint32_t nodes, edges, ways, rules, crc, map_crc, no, eo, wo, ro, size;
  struct read_page {
    uint32_t id;
    uint8_t bytes[1024];
  } *cache;
} graph_t;
static int graph_open(graph_t *g, const char *map, pos_io_progress_t p,
                      void *ctx) {
  char path_name[POS_FS_PATH];
  if (snprintf(path_name, sizeof(path_name), "%s.pnav", map) >=
      (int)sizeof(path_name))
    return -1;
  memset(g, 0, sizeof(*g));
  g->f = fopen(path_name, "rb");
  if (g->f)
    setvbuf(g->f, NULL, _IONBF, 0);
  uint8_t h[NH], mh[24];
  uint32_t size;
  FILE *mf = file(map, "rb");
  int bad = !mf || fread(mh, 1, 24, mf) != 24;
  if (mf)
    fclose(mf);
  if (bad || !g->f || fread(h, 1, NH, g->f) != NH || memcmp(h, "PNV1", 4) ||
      u32(h + 28) != 1 || pos_fs_file_size(path_name, &size))
    goto fail;
  g->map_crc = u32(h + 4);
  if (g->map_crc != u32(mh + 20))
    goto fail;
  g->nodes = u32(h + 8);
  g->edges = u32(h + 12);
  g->ways = u32(h + 16);
  g->rules = u32(h + 20);
  g->crc = u32(h + 32);
  if (!g->nodes || g->nodes > LIMIT || !g->edges || g->edges > LIMIT ||
      !g->ways || g->ways > POS_MAP_SEGMENTS || g->rules > 4096)
    goto fail;
  g->no = NH;
  g->eo = g->no + g->nodes * NODE;
  g->wo = g->eo + g->edges * EDGE;
  g->ro = g->wo + g->ways * WAY;
  if (size != g->ro + g->rules * RULE || checksum(g->f, h, 32, size, p, ctx))
    goto fail;
  g->size = size;
  g->cache = calloc(5, sizeof(*g->cache));
  if (!g->cache)
    goto fail;
  for (unsigned i = 0; i < 5; ++i)
    g->cache[i].id = INF;
  return 0;
fail:
  free(g->cache);
  g->cache = NULL;
  if (g->f)
    fclose(g->f);
  g->f = NULL;
  return -1;
}
static void graph_close(graph_t *g) {
  if (g->f)
    fclose(g->f);
  free(g->cache);
  g->f = NULL;
  g->cache = NULL;
}
static int graph_read(graph_t *g, uint32_t offset, void *out, unsigned size,
                      unsigned lane) {
  if (offset > g->size || size > g->size - offset)
    return -1;
  uint8_t *p = out;
  while (size) {
    unsigned id = offset / 1024, local = offset % 1024,
             n = size < 1024 - local ? size : 1024 - local;
    unsigned slot = lane ? lane + 1 : id % 2;
    struct read_page *page = &g->cache[slot];
    if (page->id != id) {
      unsigned available = g->size - id * 1024;
      if (available > 1024)
        available = 1024;
      if (at(g->f, id * 1024, page->bytes, available))
        return -1;
      page->id = id;
    }
    memcpy(p, page->bytes + local, n);
    p += n;
    offset += n;
    size -= n;
  }
  return 0;
}
static int gn(graph_t *g, unsigned i, uint8_t b[NODE]) {
  return i >= g->nodes || graph_read(g, g->no + i * NODE, b, NODE, 0) ? -1 : 0;
}
static int ge(graph_t *g, unsigned i, uint8_t b[EDGE]) {
  return i >= g->edges || graph_read(g, g->eo + i * EDGE, b, EDGE, 1) ||
                 u32(b) >= g->nodes || u32(b + 4) >= g->nodes ||
                 u32(b + 8) >= g->ways || !u32(b + 12) || u32(b + 16) > 7
             ? -1
             : 0;
}
static int gw(graph_t *g, unsigned i, uint8_t b[WAY]) {
  return i >= g->ways || graph_read(g, g->wo + i * WAY, b, WAY, 2) ||
                 !memchr(b + 8, 0, 64)
             ? -1
             : 0;
}
int pos_nav_compile(const char *xml, const char *map,
                    pos_io_progress_t progress, void *ctx, pos_error_t *e) {
  compiler_t *c = calloc(1, sizeof(*c));
  char *target = malloc(POS_FS_PATH);
  FILE *a = NULL, *b = NULL, *out = NULL;
  int result = -1;
  unsigned nodes = 0, edges = 0;
  if (!c || !target)
    goto done;
  if (snprintf(target, POS_FS_PATH, "%s.pnav", map) >= POS_FS_PATH ||
      !access(target, F_OK) || workspace(target, c->dir))
    goto done;
  const char *names[] = {"nodes.raw",    "segments.raw", "ways",
                         "rules",        "barriers.raw", "geometry",
                         "nodes.sorted", "edges.raw"};
  for (unsigned i = 0; i < 8; ++i)
    if (simple_path(c->names[i], c->dir, names[i]))
      goto done;
  c->xml = file(xml, "rb");
  FILE **streams[] = {&c->nodes, &c->segments, &c->ways,
                      &c->rules, &c->barriers, &c->geometry};
  for (unsigned i = 0; i < 6; ++i)
    *streams[i] = file(c->names[i], i == 5 ? "w+bx" : "wbx");
  if (!c->xml)
    goto done;
  for (unsigned i = 0; i < 6; ++i)
    if (!*streams[i])
      goto done;
  if (parse(c, progress, ctx))
    goto done;
  for (unsigned i = 0; i < 6; ++i)
    if (sync_close(streams[i]))
      goto done;
  fclose(c->xml);
  c->xml = NULL;
  if (sort_file(c->names[0], c->names[6], c->dir, "n", 16, true, progress, ctx))
    goto done;
  a = file(c->names[6], "rb");
  out = file(c->names[0], "w+b");
  if (!a || !out)
    goto done;
  uint8_t raw[16], previous[16] = {0}, node[NODE];
  bool have = false;
  while (fread(raw, 1, 16, a) == 16) {
    if (have && u64(raw) == u64(previous)) {
      if (memcmp(raw, previous, 16))
        goto done;
      continue;
    }
    memcpy(previous, raw, 16);
    have = true;
    memset(node, 0, NODE);
    memcpy(node, raw, 16);
    w32(node + 20, 7);
    if (++nodes > LIMIT || fwrite(node, 1, NODE, out) != NODE)
      goto done;
  }
  if (ferror(a) || sync_close(&out))
    goto done;
  fclose(a);
  a = NULL;
  /* Apply node access barriers by a sorted merge, preserving OSM identity. */
  unlink(c->names[6]);
  if (sort_file(c->names[4], c->names[6], c->dir, "b", 16, true, progress, ctx))
    goto done;
  a = file(c->names[0], "rb");
  b = file(c->names[6], "rb");
  out = file(c->names[4], "wb");
  if (!a || !b || !out)
    goto done;
  bool hb = fread(raw, 1, 16, b) == 16;
  for (unsigned i = 0; i < nodes; ++i) {
    if (fread(node, 1, NODE, a) != NODE)
      goto done;
    while (hb && u64(raw) < u64(node))
      hb = fread(raw, 1, 16, b) == 16;
    unsigned mask = 7;
    while (hb && u64(raw) == u64(node)) {
      mask &= u32(raw + 8);
      hb = fread(raw, 1, 16, b) == 16;
    }
    w32(node + 20, mask);
    if (fwrite(node, 1, NODE, out) != NODE)
      goto done;
  }
  if (sync_close(&out))
    goto done;
  fclose(a);
  a = NULL;
  fclose(b);
  b = NULL;
  /* Two sorted merge joins resolve endpoint IDs without random binary reads
   * or keeping the node table in RAM. */
  unlink(c->names[6]);
  if (sort_file(c->names[1], c->names[6], c->dir, "sa", 32, true, progress,
                ctx))
    goto done;
  a = file(c->names[6], "rb");
  b = file(c->names[4], "rb");
  out = file(c->names[0], "wb");
  if (!a || !b || !out)
    goto done;
  uint8_t seg[32], na[NODE], nb[NODE];
  unsigned ni = 0;
  if (fread(na, 1, NODE, b) != NODE)
    goto done;
  while (fread(seg, 1, 32, a) == 32) {
    while (u64(na) < u64(seg)) {
      if (++ni >= nodes || fread(na, 1, NODE, b) != NODE)
        goto done;
    }
    if (u64(na) != u64(seg))
      goto done;
    memcpy(seg, seg + 8, 8);
    w32(seg + 8, ni);
    w32(seg + 12, u32(na + 20));
    if (fwrite(seg, 1, 32, out) != 32)
      goto done;
  }
  if (ferror(a) || sync_close(&out))
    goto done;
  fclose(a);
  a = NULL;
  fclose(b);
  b = NULL;
  unlink(c->names[6]);
  if (sort_file(c->names[0], c->names[6], c->dir, "sb", 32, true, progress,
                ctx))
    goto done;
  a = file(c->names[6], "rb");
  b = file(c->names[4], "rb");
  out = file(c->names[7], "wbx");
  if (!a || !b || !out)
    goto done;
  ni = 0;
  if (fread(nb, 1, NODE, b) != NODE)
    goto done;
  while (fread(seg, 1, 32, a) == 32) {
    while (u64(nb) < u64(seg)) {
      if (++ni >= nodes || fread(nb, 1, NODE, b) != NODE)
        goto done;
    }
    if (u64(nb) != u64(seg))
      goto done;
    unsigned x = u32(seg + 8), y = ni, flags = u32(seg + 24);
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
      unsigned mask = (flags >> (reverse * 3)) & 7;
      mask &= u32(seg + 12) & u32(nb + 20);
      if (!mask)
        continue;
      uint8_t edge[EDGE];
      w32(edge, reverse ? y : x);
      w32(edge + 4, reverse ? x : y);
      memcpy(edge + 8, seg + 16, 8);
      w32(edge + 16, mask);
      if (++edges > LIMIT || fwrite(edge, 1, EDGE, out) != EDGE)
        goto done;
    }
    if (!(edges % 256) && tick(progress, ctx, "Connecting roads", edges, LIMIT))
      goto done;
  }
  if (ferror(a) || sync_close(&out) || !edges)
    goto done;
  fclose(a);
  a = NULL;
  fclose(b);
  b = NULL;
  unlink(c->names[6]);
  if (sort_file(c->names[7], c->names[6], c->dir, "e", EDGE, false, progress,
                ctx))
    goto done;
  a = file(c->names[4], "rb");
  b = file(c->names[6], "rb");
  out = file(c->names[5], "wb");
  if (!a || !b || !out)
    goto done;
  uint8_t h[NH] = {0}, edge[EDGE], mh[24];
  FILE *mf = file(map, "rb");
  if (!mf || fread(mh, 1, 24, mf) != 24) {
    if (mf)
      fclose(mf);
    goto done;
  }
  fclose(mf);
  memcpy(h, "PNV1", 4);
  memcpy(h + 4, mh + 20, 4);
  w32(h + 8, nodes);
  w32(h + 12, edges);
  w32(h + 16, c->nw);
  w32(h + 20, c->nr);
  w32(h + 28, 1);
  if (fwrite(h, 1, NH, out) != NH)
    goto done;
  unsigned index = 0;
  bool he = fread(edge, 1, EDGE, b) == EDGE;
  for (unsigned i = 0; i < nodes; ++i) {
    if (fread(node, 1, NODE, a) != NODE)
      goto done;
    w32(node + 16, index);
    unsigned mask = u32(node + 20), count = 0;
    while (he && u32(edge) == i) {
      ++count;
      ++index;
      he = fread(edge, 1, EDGE, b) == EDGE;
    }
    if (count > 65535)
      goto done;
    w32(node + 20, (count << 16) | mask);
    if (fwrite(node, 1, NODE, out) != NODE)
      goto done;
  }
  if (index != edges)
    goto done;
  fclose(a);
  a = NULL;
  fclose(b);
  b = NULL;
  const char *parts[] = {c->names[6], c->names[2], c->names[3]};
  uint8_t buffer[1024];
  for (unsigned i = 0; i < 3; ++i) {
    a = file(parts[i], "rb");
    if (!a)
      goto done;
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), a)))
      if (fwrite(buffer, 1, n, out) != n)
        goto done;
    if (ferror(a))
      goto done;
    fclose(a);
    a = NULL;
  }
  if (seal(&out, c->names[5], h, 32,
           NH + nodes * NODE + edges * EDGE + c->nw * WAY + c->nr * RULE))
    goto done;
  a = file(c->names[5], "rb");
  if (!a || fread(h, 1, NH, a) != NH ||
      checksum(a, h, 32,
               NH + nodes * NODE + edges * EDGE + c->nw * WAY + c->nr * RULE,
               progress, ctx))
    goto done;
  fclose(a);
  a = NULL;
  if (pos_fs_publish_new(c->names[5], target))
    goto done;
  result = 0;
done:
  if (a)
    fclose(a);
  if (b)
    fclose(b);
  if (out)
    fclose(out);
  if (c) {
    FILE *all[] = {c->xml,   c->nodes,    c->segments, c->ways,
                   c->rules, c->barriers, c->geometry};
    for (unsigned i = 0; i < 7; ++i)
      if (all[i])
        fclose(all[i]);
    if (*c->dir)
      cleanup(c->dir);
  }
  free(target);
  free(c);
  return result
             ? error(e, "Routing map preparation failed or cancelled. Existing "
                        "maps retained; try a smaller fresh download.")
             : 0;
}
/* Two 4-KiB pages per mutable SD table; indexed A* heap and labels grow on SD.
 */
typedef struct {
  uint32_t id;
  bool dirty;
  uint8_t data[4096];
} page_t;
typedef struct {
  FILE *f;
  page_t pages[2];
  unsigned hand;
  bool bad;
} pager_t;
static int flush_page(pager_t *p, page_t *s) {
  if (s->dirty && (fseek(p->f, (long)s->id * 4096, SEEK_SET) ||
                   fwrite(s->data, 1, 4096, p->f) != 4096)) {
    p->bad = true;
    return -1;
  }
  s->dirty = false;
  return 0;
}
static int transfer(pager_t *p, uint32_t offset, void *record, unsigned size,
                    bool write) {
  uint32_t id = offset / 4096;
  unsigned local = offset % 4096;
  if (local + size > 4096 || p->bad)
    return -1;
  page_t *s = NULL;
  for (unsigned i = 0; i < 2; ++i)
    if (p->pages[i].id == id)
      s = &p->pages[i];
  if (!s) {
    s = &p->pages[p->hand++ % 2];
    if (flush_page(p, s))
      return -1;
    if (fseek(p->f, (long)id * 4096, SEEK_SET) ||
        fread(s->data, 1, 4096, p->f) != 4096) {
      p->bad = true;
      return -1;
    }
    s->id = id;
  }
  if (write) {
    memcpy(s->data + local, record, size);
    s->dirty = true;
  } else
    memcpy(record, s->data + local, size);
  return 0;
}
static pager_t *pager(const char *path, unsigned size, uint8_t fill,
                      pos_io_progress_t progress, void *ctx) {
  pager_t *p = calloc(1, sizeof(*p));
  if (!p)
    return NULL;
  p->pages[0].id = p->pages[1].id = INF;
  p->f = file(path, "w+bx");
  if (!p->f) {
    free(p);
    return NULL;
  }
  memset(p->pages[0].data, fill, 4096);
  for (unsigned n = 0; n < size; n += 4096)
    if (fwrite(p->pages[0].data, 1, 4096, p->f) != 4096 ||
        tick(progress, ctx, "Preparing offline routing", n, size)) {
      fclose(p->f);
      free(p);
      return NULL;
    }
  if (fflush(p->f)) {
    fclose(p->f);
    free(p);
    return NULL;
  }
  return p;
}
static void pager_close(pager_t *p) {
  if (p) {
    fclose(p->f);
    free(p);
  }
}
typedef struct {
  pager_t *labels, *heap;
  unsigned count, limit;
} frontier_t;
static int label(frontier_t *f, unsigned i, uint8_t b[16], bool write) {
  return i >= f->limit ? -1 : transfer(f->labels, i * 16, b, 16, write);
}
static int heap_read(frontier_t *f, unsigned i, uint8_t b[8]) {
  return i >= f->count ? -1 : transfer(f->heap, i * 8, b, 8, false);
}
static int heap_write(frontier_t *f, unsigned i, uint8_t b[8]) {
  uint8_t l[16];
  if (i >= f->limit || label(f, u32(b), l, false))
    return -1;
  w32(l + 8, i);
  return transfer(f->heap, i * 8, b, 8, true) || label(f, u32(b), l, true) ? -1
                                                                           : 0;
}
static int offer(frontier_t *f, unsigned state, uint32_t cost,
                 uint32_t priority, uint32_t parent) {
  uint8_t l[16];
  if (label(f, state, l, false))
    return -1;
  if (u32(l) <= cost)
    return 0;
  unsigned pos = u32(l + 8);
  if (pos == INF) {
    if (f->count == f->limit)
      return -1;
    pos = f->count++;
  }
  w32(l, cost);
  w32(l + 4, parent);
  w32(l + 8, pos);
  w32(l + 12, INF);
  if (label(f, state, l, true))
    return -1;
  uint8_t entry[8], other[8];
  w32(entry, state);
  w32(entry + 4, priority);
  while (pos) {
    unsigned up = (pos - 1) / 2;
    if (heap_read(f, up, other))
      return -1;
    if (u32(other + 4) <= priority)
      break;
    if (heap_write(f, pos, other))
      return -1;
    pos = up;
  }
  return heap_write(f, pos, entry);
}
static int pop(frontier_t *f, unsigned *state) {
  uint8_t top[8], last[8], other[8], right[8], l[16];
  if (!f->count || heap_read(f, 0, top) || heap_read(f, f->count - 1, last))
    return -1;
  --f->count;
  unsigned pos = 0;
  while (pos * 2 + 1 < f->count) {
    unsigned child = pos * 2 + 1;
    if (heap_read(f, child, other))
      return -1;
    if (child + 1 < f->count) {
      if (heap_read(f, child + 1, right))
        return -1;
      if (u32(right + 4) < u32(other + 4)) {
        ++child;
        memcpy(other, right, 8);
      }
    }
    if (u32(last + 4) <= u32(other + 4))
      break;
    if (heap_write(f, pos, other))
      return -1;
    pos = child;
  }
  if (f->count && heap_write(f, pos, last))
    return -1;
  *state = u32(top);
  if (label(f, *state, l, false))
    return -1;
  w32(l + 8, INF);
  w32(l + 12, 0);
  return label(f, *state, l, true);
}
static double latitude(const uint8_t *node) {
  return (int32_t)u32(node + 8) / 1e6;
}
static double longitude(const uint8_t *node) {
  return (int32_t)u32(node + 12) / 1e6;
}
static int nearest(graph_t *g, double lat, double lon, unsigned mode,
                   bool destination, unsigned *index, pos_io_progress_t p,
                   void *ctx) {
  if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 80 || fabs(lon) > 180)
    return -1;
  double best = 75., sx = 111195.08 * cos(lat * PI / 180);
  bool found = false;
  uint8_t node[NODE], edge[EDGE];
  for (unsigned i = 0; i < g->edges; ++i) {
    if (!(i % 256) && tick(p, ctx, "Finding nearby roads", i, g->edges))
      return -1;
    if (ge(g, i, edge))
      return -1;
    if (!(u32(edge + 16) & mode))
      continue;
    for (unsigned end = 0; end < (destination ? 2u : 1u); ++end) {
      unsigned n = u32(edge + end * 4);
      if (gn(g, n, node))
        return -1;
      double d = hypot((lat - latitude(node)) * 111195.08,
                       (lon - longitude(node)) * sx);
      if (d <= best) {
        best = d;
        *index = n;
        found = true;
      }
    }
  }
  return found ? 0 : -1;
}
static bool contains(const char *text, const char *query) {
  for (; *text; ++text)
    if (!strncasecmp(text, query, strlen(query)))
      return true;
  return false;
}
int pos_nav_search(const char *map, const char *query, double lat, double lon,
                   unsigned mode, pos_nav_place_t results[POS_NAV_RESULTS],
                   unsigned *count, pos_io_progress_t p, void *ctx,
                   pos_error_t *e) {
  graph_t g;
  if (!query || !*query || strlen(query) > 63 || !count || !results ||
      !isfinite(lat) || !isfinite(lon) || fabs(lat) > 80 || fabs(lon) > 180 ||
      !(mode == 1 || mode == 2 || mode == 4))
    return error(e, "Enter a road name and travel mode.");
  *count = 0;
  if (graph_open(&g, map, p, ctx))
    return error(e,
                 "Routing data missing/damaged. Download this region again.");
  double distances[POS_NAV_RESULTS];
  for (unsigned i = 0; i < POS_NAV_RESULTS; ++i)
    distances[i] = HUGE_VAL;
  uint8_t way[WAY], edge[EDGE];
  int result = 0;
  /* Search actual eligible edge endpoints, rather than returning a remote
   * representative point or a road forbidden for the selected travel mode. */
  for (unsigned i = 0; i < g.edges; ++i) {
    if (!(i % 256) && tick(p, ctx, "Searching road names", i, g.edges)) {
      result = -1;
      break;
    }
    if (ge(&g, i, edge) || gw(&g, u32(edge + 8), way)) {
      result = -1;
      break;
    }
    if (!(u32(edge + 16) & mode) || !*(way + 8) ||
        !contains((char *)way + 8, query))
      continue;
    uint8_t n[NODE];
    if (gn(&g, u32(edge), n)) {
      result = -1;
      break;
    }
    double d = pos_location_distance(lat, lon, latitude(n), longitude(n));
    unsigned slot = *count;
    for (unsigned j = 0; j < *count; ++j)
      if (!strcasecmp(results[j].name, (char *)way + 8)) {
        slot = j;
        break;
      }
    if (slot == *count) {
      if (*count < POS_NAV_RESULTS)
        ++*count;
      else {
        slot = 0;
        for (unsigned j = 1; j < *count; ++j)
          if (distances[j] > distances[slot])
            slot = j;
      }
    }
    if (d < distances[slot]) {
      snprintf(results[slot].name, sizeof(results[slot].name), "%.63s",
               (char *)way + 8);
      results[slot].latitude = latitude(n);
      results[slot].longitude = longitude(n);
      distances[slot] = d;
    }
    if (!(i % 256) && tick(p, ctx, "Searching road names", i, g.edges)) {
      result = -1;
      break;
    }
  }
  graph_close(&g);
  return result ? error(e, "Road search cancelled or damaged map.") : 0;
}
static int permitted(graph_t *g, const uint8_t previous[EDGE],
                     const uint8_t next[EDGE], unsigned mode) {
  uint8_t a[WAY], b[WAY], n[NODE];
  if (gw(g, u32(next + 8), b))
    return -1;
  uint64_t from = 0, via = 0;
  if (previous) {
    if (gw(g, u32(previous + 8), a) || gn(g, u32(previous + 4), n))
      return -1;
    from = u64(a);
    via = u64(n);
  }
  for (unsigned i = 0; i < g->rules; ++i) {
    uint8_t r[RULE];
    if (graph_read(g, g->ro + i * RULE, r, RULE, 3))
      return -1;
    if (!(u32(r + 24) & mode))
      continue;
    unsigned kind = u32(r + 28);
    if (kind & 8) {
      if (u64(r) == u64(b) || from == u64(r))
        return 0;
      continue;
    }
    if (!previous || u64(r) != from || u64(r + 16) != via)
      continue;
    bool match = u64(r + 8) == u64(b);
    if (match && (kind & 2))
      match = u32(next + 4) == u32(previous);
    else if (match && (kind & 4) && u64(a) == u64(b))
      match = u32(next + 4) != u32(previous);
    if ((kind & 1) ? !match : match)
      return 0;
  }
  return 1;
}
static uint32_t heuristic(graph_t *g, unsigned node, const uint8_t goal[NODE]) {
  uint8_t n[NODE];
  if (gn(g, node, n))
    return INF;
  double d = pos_location_distance(latitude(n), longitude(n), latitude(goal),
                                   longitude(goal)) *
             100;
  return isfinite(d) && d < INF ? (uint32_t)d : INF;
}
struct pos_nav_route {
  FILE *f;
  unsigned points, total, mode;
  uint32_t graph_crc;
};
static int rp(pos_nav_route_t *r, unsigned i, uint8_t b[POINT]) {
  return i >= r->points || at(r->f, NH + i * POINT, b, POINT) ||
                 !memchr(b + 24, 0, 56) ||
                 llabs((int64_t)(int32_t)u32(b)) > 90000000 ||
                 llabs((int64_t)(int32_t)u32(b + 4)) > 180000000
             ? -1
             : 0;
}
int pos_nav_calculate(const char *map, const char *destination, double from_lat,
                      double from_lon, double to_lat, double to_lon,
                      unsigned mode, pos_io_progress_t progress, void *ctx,
                      pos_error_t *e) {
  graph_t g = {0};
  frontier_t f = {0};
  char (*paths)[POS_FS_PATH] = calloc(5, POS_FS_PATH);
  FILE *reverse = NULL, *out = NULL;
  int result = -1;
  unsigned start = 0, goal = 0, finish = INF;
  uint8_t end[NODE];
  const char *why = "No offline route in this region for that travel mode.";
  if (!paths || !(mode == 1 || mode == 2 || mode == 4) ||
      graph_open(&g, map, progress, ctx)) {
    why = "Routing graph missing/damaged. Download a fresh region.";
    goto done;
  }
  if (nearest(&g, from_lat, from_lon, mode, false, &start, progress, ctx) ||
      nearest(&g, to_lat, to_lon, mode, true, &goal, progress, ctx) ||
      gn(&g, goal, end)) {
    why = "Choose a point within 75m of a usable road. A larger downloaded "
          "region may be needed.";
    goto done;
  }
  if (!access(destination, F_OK) || workspace(destination, paths[0])) {
    why = "Route destination exists or temporary SD space is unavailable.";
    goto done;
  }
  const char *names[] = {"labels", "heap", "reverse", "route"};
  for (unsigned i = 0; i < 4; ++i)
    if (simple_path(paths[i + 1], paths[0], names[i]))
      goto done;
  f.limit = g.edges;
  f.labels = pager(paths[1], g.edges * 16, 255, progress, ctx);
  f.heap = pager(paths[2], g.edges * 8, 0, progress, ctx);
  if (!f.labels || !f.heap) {
    why = "Not enough RAM/SD space for the bounded routing workspace, or "
          "cancelled.";
    goto done;
  }
  uint8_t node[NODE], edge[EDGE];
  if (gn(&g, start, node))
    goto done;
  unsigned first = u32(node + 16), count = u32(node + 20) >> 16;
  if (start != goal)
    for (unsigned i = 0; i < count; ++i) {
      unsigned state = first + i;
      if (ge(&g, state, edge))
        goto done;
      if (!(u32(edge + 16) & mode))
        continue;
      int allow = permitted(&g, NULL, edge, mode);
      if (allow < 0)
        goto done;
      if (!allow)
        continue;
      uint32_t h = heuristic(&g, u32(edge + 4), end), cost = u32(edge + 12);
      if (h == INF || cost >= INF - h)
        continue;
      if (offer(&f, state, cost, cost + h, INF))
        goto done;
    }
  for (unsigned visited = 0; f.count; ++visited) {
    unsigned state;
    uint8_t prev[EDGE], l[16];
    if (pop(&f, &state) || ge(&g, state, prev) || label(&f, state, l, false))
      goto done;
    unsigned current = u32(prev + 4);
    if (current == goal) {
      finish = state;
      break;
    }
    if (gn(&g, current, node))
      goto done;
    first = u32(node + 16);
    count = u32(node + 20) >> 16;
    for (unsigned j = 0; j < count; ++j) {
      unsigned next = first + j;
      uint8_t nl[16];
      if (ge(&g, next, edge) || label(&f, next, nl, false))
        goto done;
      if (!(u32(edge + 16) & mode) || u32(nl + 12) == 0)
        continue;
      int allow = permitted(&g, prev, edge, mode);
      if (allow < 0)
        goto done;
      if (!allow)
        continue;
      uint32_t h = heuristic(&g, u32(edge + 4), end), cost = u32(l),
               length = u32(edge + 12);
      if (h == INF || cost >= INF - length || cost + length >= INF - h)
        continue;
      cost += length;
      if (offer(&f, next, cost, cost + h, state))
        goto done;
    }
    if (!(visited % 32) &&
        tick(progress, ctx, "Calculating offline route", visited, g.edges)) {
      why = "Routing cancelled; previous saved routes retained.";
      goto done;
    }
  }
  if (start != goal && finish == INF)
    goto done;
  reverse = file(paths[3], "w+bx");
  if (!reverse)
    goto done;
  unsigned points = 0, total = 0;
  uint8_t record[POINT] = {0}, l[16];
  /* Reverse predecessor walk is bounded by edge count, never recursive. */
  for (unsigned s = finish; s != INF;) {
    if (++points > g.edges || ge(&g, s, edge) || label(&f, s, l, false) ||
        gn(&g, u32(edge + 4), node))
      goto done;
    memset(record, 0, sizeof(record));
    memcpy(record, node + 8, 8);
    w32(record + 8, u32(l));
    w32(record + 12, u32(edge + 4));
    w32(record + 16, u32(edge + 8));
    uint8_t way[WAY];
    if (gw(&g, u32(edge + 8), way))
      goto done;
    snprintf((char *)record + 24, 56, "%.55s", (char *)way + 8);
    if (fwrite(record, 1, POINT, reverse) != POINT)
      goto done;
    if (points == 1)
      total = u32(l);
    s = u32(l + 4);
  }
  memset(record, 0, sizeof(record));
  if (gn(&g, start, node))
    goto done;
  memcpy(record, node + 8, 8);
  w32(record + 12, start);
  if (fwrite(record, 1, POINT, reverse) != POINT || fflush(reverse))
    goto done;
  ++points;
  out = file(paths[4], "wbx");
  if (!out)
    goto done;
  uint8_t h[NH] = {0};
  memcpy(h, "PRT1", 4);
  w32(h + 4, g.crc);
  w32(h + 8, points);
  w32(h + 12, total);
  w32(h + 16, mode);
  if (fwrite(h, 1, NH, out) != NH)
    goto done;
  uint8_t next[POINT];
  for (unsigned i = 0; i < points; ++i) {
    if (at(reverse, (points - 1 - i) * POINT, record, POINT))
      goto done;
    /* Point names describe the edge leaving this point. */
    if (i + 1 < points) {
      if (at(reverse, (points - 2 - i) * POINT, next, POINT))
        goto done;
      memcpy(record + 16, next + 16, 4);
      memcpy(record + 24, next + 24, 56);
    }
    if (fwrite(record, 1, POINT, out) != POINT)
      goto done;
  }
  if (seal(&out, paths[4], h, 20, NH + points * POINT))
    goto done;
  /* Check final CRC before atomic publication; no old route is replaced. */
  out = file(paths[4], "rb");
  if (!out || fread(h, 1, NH, out) != NH ||
      checksum(out, h, 20, NH + points * POINT, progress, ctx))
    goto done;
  fclose(out);
  out = NULL;
  if (pos_fs_publish_new(paths[4], destination))
    goto done;
  result = 0;
done:
  if (reverse)
    fclose(reverse);
  if (out)
    fclose(out);
  pager_close(f.labels);
  pager_close(f.heap);
  if (g.f)
    graph_close(&g);
  if (paths && *paths[0])
    cleanup(paths[0]);
  free(paths);
  return result ? error(e, why) : 0;
}
pos_nav_route_t *pos_nav_open(const char *route, const char *map,
                              pos_io_progress_t progress, void *ctx,
                              pos_error_t *e) {
  graph_t g;
  if (graph_open(&g, map, progress, ctx)) {
    error(e, "Routing map missing or changed. Calculate a new route.");
    return NULL;
  }
  uint32_t graph_crc = g.crc;
  graph_close(&g);
  pos_nav_route_t *r = calloc(1, sizeof(*r));
  uint8_t h[NH];
  uint32_t size;
  if (!r) {
    error(e, "Not enough memory to open a saved route.");
    return NULL;
  }
  r->f = file(route, "rb");
  if (!r->f || fread(h, 1, NH, r->f) != NH || memcmp(h, "PRT1", 4) ||
      u32(h + 4) != graph_crc || pos_fs_file_size(route, &size))
    goto fail;
  r->graph_crc = graph_crc;
  r->points = u32(h + 8);
  r->total = u32(h + 12);
  r->mode = u32(h + 16);
  if (!r->points || r->points > LIMIT + 1 ||
      !(r->mode == 1 || r->mode == 2 || r->mode == 4) ||
      size != NH + r->points * POINT ||
      checksum(r->f, h, 20, size, progress, ctx))
    goto fail;
  uint32_t last = 0;
  for (unsigned i = 0; i < r->points; ++i) {
    uint8_t point[POINT];
    if (rp(r, i, point) || u32(point + 8) < last || u32(point + 8) > r->total)
      goto fail;
    last = u32(point + 8);
  }
  if (last != r->total)
    goto fail;
  return r;
fail:
  pos_nav_close(r);
  error(e, "Saved route damaged, incomplete, or for another map.");
  return NULL;
}
void pos_nav_close(pos_nav_route_t *r) {
  if (r) {
    if (r->f)
      fclose(r->f);
    free(r);
  }
}
void pos_nav_destination(pos_nav_route_t *r, double *lat, double *lon) {
  uint8_t b[POINT];
  if (!rp(r, r->points - 1, b)) {
    *lat = (int32_t)u32(b) / 1e6;
    *lon = (int32_t)u32(b + 4) / 1e6;
  }
}
static double plat(const uint8_t *p) { return (int32_t)u32(p) / 1e6; }
static double plon(const uint8_t *p) { return (int32_t)u32(p + 4) / 1e6; }
int pos_nav_guide(pos_nav_route_t *r, double lat, double lon, unsigned previous,
                  pos_nav_guidance_t *guide, pos_error_t *e) {
  if (!r || !guide || !isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 ||
      fabs(lon) > 180)
    return error(e, "Invalid route position.");
  if (previous >= r->points)
    previous = INF;
  memset(guide, 0, sizeof(*guide));
  uint8_t a[POINT], b[POINT], c[POINT];
  double best = HUGE_VAL, progress_cm = 0;
  unsigned best_i = 0;
  if (r->points == 1) {
    if (rp(r, 0, a))
      goto bad;
    best = pos_location_distance(lat, lon, plat(a), plon(a));
  } else {
    unsigned begin = previous == INF ? 0
                     : previous > 3  ? previous - 3
                                     : 0,
             end = previous == INF ? r->points - 1 : previous + 40;
    if (end >= r->points)
      end = r->points - 1;
    if (begin >= end)
      begin = end - 1;
    for (unsigned i = begin; i < end; ++i) {
      if (rp(r, i, a) || rp(r, i + 1, b))
        goto bad;
      double scale = 111195.08,
             dx = (plon(b) - plon(a)) * cos(lat * PI / 180) * scale,
             dy = (plat(b) - plat(a)) * scale,
             x = (lon - plon(a)) * cos(lat * PI / 180) * scale,
             y = (lat - plat(a)) * scale, len = dx * dx + dy * dy,
             t = len ? fmax(0, fmin(1, (x * dx + y * dy) / len)) : 0,
             d = hypot(x - t * dx, y - t * dy);
      if (d < best - .25) {
        best = d;
        best_i = i;
        progress_cm = u32(a + 8) + t * (u32(b + 8) - u32(a + 8));
      }
    }
  }
  guide->segment = best_i;
  guide->off_route = best > 40;
  guide->remaining_m = (unsigned)ceil(fmax(0, r->total - progress_cm) / 100);
  if (rp(r, r->points - 1, c))
    goto bad;
  guide->arrived = guide->remaining_m < 15 &&
                   pos_location_distance(lat, lon, plat(c), plon(c)) < 20;
  guide->turn = guide->arrived ? POS_NAV_FINISH : POS_NAV_STRAIGHT;
  guide->next_m = guide->remaining_m;
  if (rp(r, best_i, a))
    goto bad;
  snprintf(guide->road, sizeof(guide->road), "%s", (char *)a + 24);
  for (unsigned i = best_i + 1; i + 1 < r->points; ++i) {
    if (rp(r, i - 1, a) || rp(r, i, b) || rp(r, i + 1, c))
      goto bad;
    if (u32(a + 16) == u32(b + 16))
      continue;
    double incoming = pos_map_bearing(plat(a), plon(a), plat(b), plon(b)),
           outgoing = pos_map_bearing(plat(b), plon(b), plat(c), plon(c));
    double angle = fmod(outgoing - incoming + 540, 360) - 180;
    guide->turn = fabs(angle) > 150 ? POS_NAV_UTURN
                  : angle > 30      ? POS_NAV_RIGHT
                  : angle < -30     ? POS_NAV_LEFT
                                    : POS_NAV_STRAIGHT;
    guide->next_m = (unsigned)ceil(fmax(0, u32(b + 8) - progress_cm) / 100);
    snprintf(guide->road, sizeof(guide->road), "%s", (char *)b + 24);
    break;
  }
  return 0;
bad:
  return error(e,
               "Route read failed; stop guidance and reopen the saved route.");
}
int pos_nav_raster(pos_nav_route_t *r, double lat, double lon, unsigned width,
                   uint8_t pixels[POS_MAP_PIXELS], pos_error_t *e) {
  memset(pixels, 0, POS_MAP_PIXELS);
  uint8_t a[POINT], b[POINT];
  for (unsigned i = 1; i < r->points; ++i) {
    if (rp(r, i - 1, a) || rp(r, i, b))
      return error(e, "Route read failed.");
    int x, y, xx, yy;
    pos_map_project(plat(a), plon(a), lat, lon, width, &x, &y);
    pos_map_project(plat(b), plon(b), lat, lon, width, &xx, &yy);
    pos_map_line(pixels, x, y, xx, yy);
  }
  return 0;
}

unsigned pos_nav_mode(const pos_nav_route_t *r) { return r ? r->mode : 0; }
