#include "pos_settings.h"
#include "cJSON.h"
#include "pos_fs.h"
#include "pos_install.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void pos_settings_defaults(pos_settings_t *s) {
  memset(s, 0, sizeof(*s));
  strcpy(s->display_name, "Stevie");
  strcpy(s->device_name, "PANPUTER");
  strcpy(s->timezone, "UTC0");
  s->brightness = 70;
}

bool pos_settings_valid(const pos_settings_t *s) {
  if (!memchr(s->display_name, 0, sizeof(s->display_name)) ||
      !*s->display_name || !memchr(s->device_name, 0, sizeof(s->device_name)) ||
      !*s->device_name || !memchr(s->timezone, 0, sizeof(s->timezone)) ||
      !*s->timezone || s->brightness < 10 || s->brightness > 100)
    return false;
  for (const char *p = s->display_name; *p; ++p)
    if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
      return false;
  for (const char *p = s->device_name; *p; ++p)
    if (!isalnum((unsigned char)*p) && *p != '-')
      return false;
  for (const char *p = s->timezone; *p; ++p)
    if (!isalnum((unsigned char)*p) && !strchr("<>+-:,./", *p))
      return false;
  return true;
}

static const char *str(const cJSON *j, const char *name) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, name);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int read_slot(const char *root, int slot, pos_settings_t *s) {
  char path[POS_FS_PATH], relative[64], buffer[2049];
  snprintf(relative, sizeof(relative), "POS/Users/settings.%d.json", slot);
  if (pos_fs_join(path, root, relative))
    return -1;
  FILE *f = fopen(path, "rb");
  if (!f)
    return errno == ENOENT ? 1 : -1;
  size_t n = fread(buffer, 1, sizeof(buffer), f);
  int bad = ferror(f);
  if (fclose(f))
    bad = 1;
  if (bad || n >= sizeof(buffer) || memchr(buffer, 0, n))
    return -1;
  buffer[n] = 0;
  return pos_settings_decode(buffer, n, s);
}

int pos_settings_decode(const char *buffer, size_t n, pos_settings_t *s) {
  if (!pos_fs_json_safe(buffer, n))
    return -1;
  cJSON *envelope = cJSON_ParseWithLengthOpts(buffer, n + 1, NULL, true);
  const char *body = str(envelope, "payload"), *hash = str(envelope, "sha256");
  char digest[65];
  int result = -1;
  if (!cJSON_IsObject(envelope) || cJSON_GetArraySize(envelope) != 2 || !body ||
      !hash || strlen(hash) != 64 || !pos_fs_json_safe(body, strlen(body)) ||
      pos_hash_data(body, strlen(body), digest) || strcmp(hash, digest))
    goto done;
  cJSON *j = cJSON_ParseWithOpts(body, NULL, true);
  if (!j)
    goto done;
  cJSON *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *generation = cJSON_GetObjectItemCaseSensitive(j, "generation");
  if (!cJSON_IsNumber(format) || format->valuedouble != 1) {
    result = -2;
    cJSON_Delete(j);
    goto done;
  }
  const char *name = str(j, "display_name"), *device = str(j, "device_name"),
             *tz = str(j, "timezone");
  cJSON *bright = cJSON_GetObjectItemCaseSensitive(j, "brightness"),
        *complete = cJSON_GetObjectItemCaseSensitive(j, "setup_complete");
  if (cJSON_GetArraySize(j) != 7 || !name ||
      strlen(name) >= sizeof(s->display_name) || !device ||
      strlen(device) >= sizeof(s->device_name) || !tz ||
      strlen(tz) >= sizeof(s->timezone) || !cJSON_IsNumber(generation) ||
      generation->valuedouble < 1 || generation->valuedouble > UINT32_MAX ||
      generation->valuedouble != (uint32_t)generation->valuedouble ||
      !cJSON_IsNumber(bright) || bright->valuedouble < 10 ||
      bright->valuedouble > 100 ||
      bright->valuedouble != (int)bright->valuedouble ||
      !cJSON_IsBool(complete)) {
    cJSON_Delete(j);
    goto done;
  }
  memset(s, 0, sizeof(*s));
  strcpy(s->display_name, name);
  strcpy(s->device_name, device);
  strcpy(s->timezone, tz);
  s->brightness = bright->valueint;
  s->generation = (uint32_t)generation->valuedouble;
  s->setup_complete = cJSON_IsTrue(complete);
  result = pos_settings_valid(s) ? 0 : -1;
  cJSON_Delete(j);
done:
  cJSON_Delete(envelope);
  return result;
}

int pos_settings_load(const char *root, pos_settings_t *s) {
  pos_settings_t a, b;
  int ra = read_slot(root, 0, &a), rb = read_slot(root, 1, &b);
  if (ra == -2 || rb == -2)
    return -2;
  if (!ra || !rb) {
    *s = !ra && (rb || a.generation >= b.generation) ? a : b;
    return 0;
  }
  pos_settings_defaults(s);
  return ra == 1 && rb == 1 ? 1 : -1;
}

int pos_settings_save(const char *root, pos_settings_t *s) {
  pos_settings_t old;
  int status = pos_settings_load(root, &old);
  if (status == -2 || !pos_settings_valid(s) ||
      (!status && old.generation == UINT32_MAX))
    return -1;
  uint32_t generation = !status ? old.generation + 1 : 1;
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddNumberToObject(j, "generation", generation);
  cJSON_AddStringToObject(j, "display_name", s->display_name);
  cJSON_AddStringToObject(j, "device_name", s->device_name);
  cJSON_AddStringToObject(j, "timezone", s->timezone);
  cJSON_AddNumberToObject(j, "brightness", s->brightness);
  cJSON_AddBoolToObject(j, "setup_complete", s->setup_complete);
  char *body = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!body)
    return -1;
  char hash[65];
  if (pos_hash_data(body, strlen(body), hash)) {
    free(body);
    return -1;
  }
  j = cJSON_CreateObject();
  if (!j) {
    free(body);
    return -1;
  }
  cJSON_AddStringToObject(j, "payload", body);
  cJSON_AddStringToObject(j, "sha256", hash);
  free(body);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!text)
    return -1;
  char directory[POS_FS_PATH], path[POS_FS_PATH], temp[POS_FS_PATH],
      relative[100];
  int result = -1;
  if (pos_fs_join(directory, root, "POS/Users") || pos_fs_mkdirs(directory))
    goto done;
  snprintf(relative, sizeof(relative), "POS/Users/settings.%u.json",
           (unsigned)(generation % 2));
  if (pos_fs_join(path, root, relative) ||
      snprintf(temp, sizeof(temp), "%s.part", path) >= (int)sizeof(temp))
    goto done;
  if (unlink(temp) && errno != ENOENT)
    goto done;
  if (pos_fs_write_new(temp, text, strlen(text)))
    goto done;
  /* Retain the other good slot. Preserve a corrupt target instead of truncating
   * it. */
  struct stat st;
  if (!stat(path, &st)) {
    pos_settings_t candidate;
    int valid = read_slot(root, generation % 2, &candidate);
    if (valid) {
      bool moved = false;
      for (unsigned i = 0; i < 100; ++i) {
        char saved[POS_FS_PATH];
        if (snprintf(saved, sizeof(saved), "%s.corrupt-%u", path, i) >=
            (int)sizeof(saved))
          break;
        if (stat(saved, &st) && errno == ENOENT) {
          moved = rename(path, saved) == 0;
          break;
        }
      }
      if (!moved)
        goto done;
    } else if (unlink(path))
      goto done;
  }
  if (rename(temp, path))
    goto done;
  pos_settings_t checked;
  if (read_slot(root, generation % 2, &checked) ||
      checked.generation != generation)
    goto done;
  s->generation = generation;
  result = 0;
done:
  free(text);
  return result;
}
