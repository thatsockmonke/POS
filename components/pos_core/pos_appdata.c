#include "pos_appdata.h"
#include "cJSON.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int fail(pos_error_t *e, const char *text, int code) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return code;
}
static bool app_valid(const char *a) {
  size_t n = strlen(a);
  if (!n || n > 32)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (!((a[i] >= 'a' && a[i] <= 'z') || (a[i] >= '0' && a[i] <= '9') ||
          a[i] == '-'))
      return false;
  return true;
}
static bool json_valid(const char *s) {
  if (!pos_fs_json_safe(s, strlen(s)))
    return false;
  cJSON *j = cJSON_ParseWithOpts(s, NULL, true);
  bool valid = j != NULL;
  cJSON_Delete(j);
  return valid;
}
static uint32_t checksum(const char *app, const pos_appdata_t *d) {
  char h[100];
  int n = snprintf(h, sizeof(h), "%s\n%u\n%u\n", app, (unsigned)d->schema,
                   (unsigned)d->generation);
  return pos_crc32(pos_crc32(0, h, n), d->json, strlen(d->json));
}
static int filename(const char *root, const char *app, unsigned slot,
                    char path[POS_FS_PATH]) {
  char relative[100];
  snprintf(relative, sizeof(relative), "POS/Users/Owner/Saves/%s/slot-%u.json",
           app, slot);
  return !app_valid(app) || !pos_files_mutable(root, relative) ||
                 pos_fs_join(path, root, relative)
             ? -1
             : 0;
}
static int read_slot(const char *path, const char *app, pos_appdata_t *d) {
  struct stat st;
  if (pos_fs_lstat(path, &st))
    return errno == ENOENT ? 1 : -1;
  if (!S_ISREG(st.st_mode) || st.st_size > 3072)
    return -1;
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  char *s = malloc(3073);
  if (!s) {
    fclose(f);
    return -1;
  }
  size_t n = fread(s, 1, 3072, f);
  bool bad = ferror(f);
  if (fclose(f))
    bad = true;
  s[n] = 0;
  cJSON *j = !bad && pos_fs_json_safe(s, n) ? cJSON_ParseWithOpts(s, NULL, true)
                                            : NULL;
  free(s);
  cJSON *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *id = cJSON_GetObjectItemCaseSensitive(j, "app"),
        *schema = cJSON_GetObjectItemCaseSensitive(j, "schema"),
        *generation = cJSON_GetObjectItemCaseSensitive(j, "generation"),
        *payload = cJSON_GetObjectItemCaseSensitive(j, "payload"),
        *crc = cJSON_GetObjectItemCaseSensitive(j, "crc32");
  int result = -1;
  if (cJSON_IsNumber(format) && format->valuedouble > 1)
    result = -2;
  else if (cJSON_IsObject(j) && cJSON_GetArraySize(j) == 6 &&
           cJSON_IsNumber(format) && format->valuedouble == 1 &&
           cJSON_IsString(id) && !strcmp(id->valuestring, app) &&
           cJSON_IsNumber(schema) && schema->valuedouble >= 0 &&
           schema->valuedouble <= UINT32_MAX &&
           schema->valuedouble == (uint32_t)schema->valuedouble &&
           cJSON_IsNumber(generation) && generation->valuedouble >= 1 &&
           generation->valuedouble <= UINT32_MAX &&
           generation->valuedouble == (uint32_t)generation->valuedouble &&
           cJSON_IsString(payload) &&
           strlen(payload->valuestring) <= POS_APPDATA_MAX &&
           json_valid(payload->valuestring) && cJSON_IsString(crc) &&
           strlen(crc->valuestring) == 8) {
    d->schema = (uint32_t)schema->valuedouble;
    d->generation = (uint32_t)generation->valuedouble;
    strcpy(d->json, payload->valuestring);
    char expected[9];
    snprintf(expected, sizeof(expected), "%08x", (unsigned)checksum(app, d));
    if (!strcmp(expected, crc->valuestring))
      result = 0;
  }
  cJSON_Delete(j);
  return result;
}
static int load(const char *root, const char *app, pos_appdata_t *out,
                pos_error_t *e) {
  pos_appdata_t *d = calloc(2, sizeof(*d));
  char path[POS_FS_PATH];
  if (!d)
    return fail(e, "Not enough memory for save metadata.", -1);
  int status[2];
  for (unsigned i = 0; i < 2; ++i) {
    status[i] = filename(root, app, i, path) ? -1 : read_slot(path, app, d + i);
    if (!status[i] && d[i].generation % 2 != i)
      status[i] = -1;
  }
  int result;
  if (status[0] == -2 || status[1] == -2)
    result = -2;
  else if (!status[0] || !status[1]) {
    unsigned choice = status[0]                           ? 1
                      : status[1]                         ? 0
                      : d[1].generation > d[0].generation ? 1
                                                          : 0;
    if (!status[0] && !status[1] && d[0].generation == d[1].generation &&
        (d[0].schema != d[1].schema || strcmp(d[0].json, d[1].json)))
      result = -1;
    else {
      *out = d[choice];
      result = 0;
    }
    /* A valid newer schema in EITHER slot must never be silently retired. */
    if (!result && !status[0] && !status[1] &&
        d[1 - choice].schema > out->schema) {
      *out = d[1 - choice];
    }
  } else
    result = status[0] == 1 && status[1] == 1 ? 1 : -1;
  free(d);
  if (result < 0)
    fail(e,
         result == -2 ? "Future save format preserved; writes disabled."
                      : "Save metadata damaged; original files retained.",
         result);
  return result;
}
int pos_appdata_load(const char *root, const char *app, uint32_t schema,
                     pos_appdata_migrator_t migrate, void *ctx,
                     pos_appdata_t *out, pos_error_t *e) {
  int result = load(root, app, out, e);
  if (result)
    return result;
  if (out->schema > schema || schema - out->schema > 16 ||
      (out->schema < schema && !migrate))
    return fail(e, "Unsupported app schema preserved; writes disabled.", -2);
  if (out->schema == schema)
    return 0;
  char *next = malloc(POS_APPDATA_MAX + 1);
  if (!next)
    return fail(e, "Cannot allocate migration metadata.", -1);
  while (out->schema < schema) {
    memset(next, 0, POS_APPDATA_MAX + 1);
    if (migrate(ctx, out->schema, out->json, next, POS_APPDATA_MAX + 1) ||
        !memchr(next, 0, POS_APPDATA_MAX + 1) || !json_valid(next)) {
      free(next);
      return fail(e, "No compatible migration; source retained.", -2);
    }
    strcpy(out->json, next);
    ++out->schema;
  }
  free(next);
  return 2;
}
int pos_appdata_save(const char *root, const char *app, pos_appdata_t *d,
                     pos_error_t *e) {
  if (!app_valid(app) || !memchr(d->json, 0, sizeof(d->json)) ||
      !json_valid(d->json))
    return fail(e, "Invalid app save.", -1);
  pos_appdata_t *old = calloc(1, sizeof(*old));
  if (!old)
    return -1;
  int loaded = load(root, app, old, e);
  if (loaded < 0 ||
      (!loaded &&
       (old->schema > d->schema || old->generation != d->generation)) ||
      (loaded == 1 && d->generation) ||
      (!loaded && old->generation == UINT32_MAX)) {
    free(old);
    return fail(e, "Save conflict or future schema; files retained.", -2);
  }
  /* Preserve the exact pre-migration envelope permanently before writing. */
  char path[POS_FS_PATH], previous[POS_FS_PATH], relative[160];
  if (!loaded && old->schema < d->schema) {
    snprintf(relative, sizeof(relative),
             "POS/Users/Owner/Saves/%s/migration-%u-%u.json", app,
             (unsigned)old->schema, (unsigned)old->generation);
    if (filename(root, app, old->generation % 2, path) ||
        pos_fs_join(previous, root, relative)) {
      free(old);
      return -1;
    }
    struct stat st;
    if (pos_fs_lstat(previous, &st)) {
      if (pos_files_copy(root, path + strlen(root) + 1, relative, NULL, NULL,
                         e)) {
        free(old);
        return -1;
      }
    } else {
      pos_appdata_t *backup = calloc(1, sizeof(*backup));
      bool ok = backup && !read_slot(previous, app, backup) &&
                backup->schema == old->schema &&
                backup->generation == old->generation &&
                !strcmp(backup->json, old->json);
      free(backup);
      if (!ok) {
        free(old);
        return fail(e, "Migration backup conflict; original retained.", -1);
      }
    }
  }
  uint32_t generation = loaded == 1 ? 1 : old->generation + 1;
  free(old);
  if (filename(root, app, generation % 2, path))
    return -1;
  strcpy(previous, path);
  *strrchr(previous, '/') = 0;
  if (pos_fs_mkdirs(previous) || snprintf(previous, sizeof(previous), "%s.part",
                                          path) >= (int)sizeof(previous))
    return -1;
  struct stat st;
  if (!pos_fs_lstat(previous, &st))
    return fail(e,
                "Interrupted save retained; inspect slot .part in Files before "
                "retrying.",
                -1);
  uint32_t before = d->generation;
  d->generation = generation;
  cJSON *j = cJSON_CreateObject();
  if (!j) {
    d->generation = before;
    return -1;
  }
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddStringToObject(j, "app", app);
  cJSON_AddNumberToObject(j, "schema", d->schema);
  cJSON_AddNumberToObject(j, "generation", d->generation);
  cJSON_AddStringToObject(j, "payload", d->json);
  char crc[9];
  snprintf(crc, sizeof(crc), "%08x", (unsigned)checksum(app, d));
  cJSON_AddStringToObject(j, "crc32", crc);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  int result = !text || pos_fs_write_new(previous, text, strlen(text));
  free(text);
  pos_appdata_t *verified = calloc(1, sizeof(*verified));
  if (!result &&
      (!verified || read_slot(previous, app, verified) ||
       verified->schema != d->schema || verified->generation != generation ||
       strcmp(verified->json, d->json)))
    result = -1;
  free(verified);
  if (!result && rename(previous, path))
    result = -1;
  if (result)
    d->generation = before;
  return result ? fail(e, "App save not committed; previous slot retained.", -1)
                : 0;
}
static int counter_value(const char *json, const char *key, int *value) {
  cJSON *j = cJSON_ParseWithOpts(json, NULL, true),
        *v = cJSON_GetObjectItemCaseSensitive(j, key);
  bool ok = cJSON_IsObject(j) && cJSON_GetArraySize(j) == 1 &&
            cJSON_IsNumber(v) && v->valuedouble >= -1000000 &&
            v->valuedouble <= 1000000 && v->valuedouble == v->valueint;
  if (ok)
    *value = v->valueint;
  cJSON_Delete(j);
  return ok ? 0 : -1;
}
static int counter_migrate(void *ctx, uint32_t schema, const char *json,
                           char *out, size_t capacity) {
  (void)ctx;
  int value;
  if (schema || counter_value(json, "value", &value))
    return -1;
  snprintf(out, capacity, "{\"count\":%d}", value);
  return 0;
}
int pos_counter_load(const char *root, int *count, pos_error_t *e) {
  pos_appdata_t *d = calloc(1, sizeof(*d));
  if (!d)
    return -1;
  int result =
      pos_appdata_load(root, "counter", 1, counter_migrate, NULL, d, e);
  if (result == 0 || result == 2) {
    if (counter_value(d->json, "count", count))
      result = -1;
    else if (result == 2 && pos_appdata_save(root, "counter", d, e))
      result = -1;
  } else if (result == 1) {
    *count = 0;
    bool found_legacy = false;
    const char *names[] = {"POS/Users/Owner/Saves/counter.txt",
                           "POS/Users/Owner/Saves/counter.previous.txt"};
    for (unsigned i = 0; i < 2; ++i) {
      char path[POS_FS_PATH], text[41];
      if (!pos_files_mutable(root, names[i]) ||
          pos_fs_join(path, root, names[i]))
        continue;
      FILE *f = fopen(path, "rb");
      if (!f)
        continue;
      found_legacy = true;
      size_t n = fread(text, 1, 40, f);
      bool bad = ferror(f);
      fclose(f);
      text[n] = 0;
      char *end;
      errno = 0;
      long value = strtol(text, &end, 10);
      if (!bad && n < 40 && !errno && end != text &&
          (!*end || (*end == '\n' && !end[1])) && value >= -1000000 &&
          value <= 1000000) {
        *count = (int)value;
        snprintf(d->json, sizeof(d->json), "{\"count\":%d}", *count);
        d->schema = 1;
        result = pos_appdata_save(root, "counter", d, e) ? -1 : 2;
        break;
      }
    }
    if (result == 1 && found_legacy)
      result =
          fail(e, "Legacy Counter save is damaged; originals retained.", -1);
  }
  free(d);
  return result;
}
int pos_counter_save(const char *root, int count, pos_error_t *e) {
  if (count < -1000000 || count > 1000000)
    return -1;
  pos_appdata_t *d = calloc(1, sizeof(*d));
  if (!d)
    return -1;
  int result =
      pos_appdata_load(root, "counter", 1, counter_migrate, NULL, d, e);
  if (result >= 0) {
    d->schema = 1;
    snprintf(d->json, sizeof(d->json), "{\"count\":%d}", count);
    result = pos_appdata_save(root, "counter", d, e);
  }
  free(d);
  return result;
}
