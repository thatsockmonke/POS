#include "pos_migrate.h"
#include "cJSON.h"
#include "pos_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

int pos_migration_settings(const char *source, const char *relative,
                           pos_settings_t *settings) {
  if (strncasecmp(relative, "POS/Users/settings.", 19))
    return 0;
  FILE *f = fopen(source, "rb");
  if (!f)
    return -1;
  char text[2049];
  size_t n = fread(text, 1, sizeof(text), f);
  int bad = ferror(f);
  if (fclose(f))
    bad = 1;
  if (bad || n >= sizeof(text))
    return -1;
  text[n] = 0;
  return pos_migration_decode(relative, text, n, settings);
}
int pos_migration_decode(const char *relative, const char *text, size_t n,
                         pos_settings_t *settings) {
  if (strncasecmp(relative, "POS/Users/settings.", 19))
    return 0;
  if (!pos_fs_json_safe(text, n))
    return -1;
  if (!strcmp(relative, "POS/Users/settings.0.json") ||
      !strcmp(relative, "POS/Users/settings.1.json"))
    return pos_settings_decode(text, n, settings) ? -1 : 1;
  if (strcmp(relative, "POS/Users/settings.json"))
    return -1;
  cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
  cJSON *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *name = cJSON_GetObjectItemCaseSensitive(j, "user_name"),
        *device = cJSON_GetObjectItemCaseSensitive(j, "device_name"),
        *zone = cJSON_GetObjectItemCaseSensitive(j, "timezone"),
        *bright = cJSON_GetObjectItemCaseSensitive(j, "brightness");
  bool ok = cJSON_IsNumber(format) && format->valuedouble == 0 &&
            (cJSON_GetArraySize(j) == 4 || cJSON_GetArraySize(j) == 5) &&
            cJSON_IsString(name) &&
            strlen(name->valuestring) < sizeof(settings->display_name) &&
            cJSON_IsString(device) &&
            strlen(device->valuestring) < sizeof(settings->device_name) &&
            cJSON_IsString(zone) &&
            strlen(zone->valuestring) < sizeof(settings->timezone) &&
            (!bright || (cJSON_IsNumber(bright) && bright->valuedouble >= 10 &&
                         bright->valuedouble <= 100 &&
                         bright->valuedouble == bright->valueint));
  if (ok) {
    pos_settings_defaults(settings);
    strcpy(settings->display_name, name->valuestring);
    strcpy(settings->device_name, device->valuestring);
    strcpy(settings->timezone, zone->valuestring);
    if (bright)
      settings->brightness = bright->valueint;
    settings->setup_complete = true;
    ok = pos_settings_valid(settings);
  }
  cJSON_Delete(j);
  return ok ? 2 : -1;
}

int pos_migration_apply(const char *root, const pos_settings_t *legacy) {
  pos_settings_t current;
  int result = pos_settings_load(root, &current);
  if (result == 1 || result == -1) {
    current = *legacy;
    return pos_settings_save(root, &current);
  }
  return 0; /* A current or future configuration always takes priority. */
}

int pos_migration_record(const char *root, const char *id, const char *source,
                         const char *destination, const char *action) {
  char relative[100], path[POS_FS_PATH], folder[POS_FS_PATH];
  if (pos_fs_join(folder, root, "POS/Users/Owner/Recovery") ||
      pos_fs_mkdirs(folder))
    return -1;
  snprintf(relative, sizeof(relative),
           "POS/Users/Owner/Recovery/restore-%.12s.jsonl", id);
  if (pos_fs_join(path, root, relative))
    return -1;
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddStringToObject(j, "source", source);
  cJSON_AddStringToObject(j, "destination", destination);
  cJSON_AddStringToObject(j, "action", action);
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!text)
    return -1;
  struct stat st;
  if (!pos_fs_lstat(path, &st) && !S_ISREG(st.st_mode)) {
    free(text);
    return -1;
  }
  FILE *f = fopen(path, "ab");
  int bad = !f;
  if (f) {
    if (fprintf(f, "%s\n", text) < 0 || fflush(f) || fsync(fileno(f)))
      bad = 1;
    if (fclose(f))
      bad = 1;
  }
  free(text);
  return bad ? -1 : 0;
}
