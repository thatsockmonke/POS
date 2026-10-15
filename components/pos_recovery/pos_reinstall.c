#include "cJSON.h"
#include "pos_fs.h"
#include "pos_recovery.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void pos_recovery_source_version(const char *root, char version[32]) {
  strcpy(version, "unknown");
  char path[POS_FS_PATH];
  pos_package_t *p = NULL;
  pos_error_t error = {0};
  if (!pos_fs_join(path, root, "POS/System/installed.pos") &&
      !pos_load_manifest(path, &p, &error)) {
    strcpy(version, p->version);
    pos_package_free(p);
  }
}

static int failure(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
int pos_reinstall_prepare(const char *root, const pos_package_t *package,
                          const pos_platform_t *platform, pos_error_t *error) {
  if (pos_check_package(root, package, platform, error))
    return -1;
  char backup[POS_FS_PATH], journal[POS_FS_PATH], old[POS_FS_PATH],
      current[POS_FS_PATH];
  if (pos_backup_find(root, backup, error) ||
      pos_fs_join(journal, root, "pos-reinstall.pos") ||
      pos_fs_join(old, root, "POS.previous") ||
      pos_fs_join(current, root, "POS"))
    return failure(error, "Path too long");
  pos_backup_info_t info;
  if (pos_backup_verify(backup, &info, platform->progress, platform->ctx,
                        error))
    return -1;
  struct stat st;
  char hash[65];
  if (pos_backup_identity_progress(backup, hash, platform->progress,
                                   platform->ctx, error))
    return -1;
  FILE *f = fopen(journal, "rb");
  bool resume = false;
  if (f) {
    char text[1025];
    size_t n = fread(text, 1, sizeof(text), f);
    int bad = ferror(f);
    fclose(f);
    if (bad || n >= sizeof(text) || memchr(text, 0, n))
      return failure(error, "Unreadable reinstall journal preserved");
    text[n] = 0;
    if (!pos_fs_json_safe(text, n))
      return failure(error, "Invalid reinstall journal preserved");
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true),
          *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
          *id = cJSON_GetObjectItemCaseSensitive(j, "package"),
          *digest = cJSON_GetObjectItemCaseSensitive(j, "backup");
    resume = cJSON_IsNumber(format) && format->valuedouble == 1 &&
             cJSON_GetArraySize(j) == 3 && cJSON_IsString(id) &&
             cJSON_IsString(digest) && !strcmp(id->valuestring, package->id) &&
             !strcmp(digest->valuestring, hash);
    cJSON_Delete(j);
    if (!resume)
      return failure(error, "A different reinstall journal exists; preserve "
                            "and inspect it first");
  } else if (errno != ENOENT)
    return failure(error, "Cannot inspect reinstall journal");
  bool previous = !stat(old, &st);
  if (!resume) {
    if (previous)
      return failure(error,
                     "POS.previous already exists; old data preserved. Copy it "
                     "to your computer before another reinstall");
    if (stat(current, &st) || !S_ISDIR(st.st_mode))
      return failure(error, "No old POS tree to reinstall");
    if (pos_backup_matches_progress(root, backup, platform->progress,
                                    platform->ctx, error))
      return -1;
    if (platform->report)
      platform->report(platform->ctx, "backup_verified", hash);
    char text[256];
    int n = snprintf(text, sizeof(text),
                     "{\"format\":1,\"package\":\"%s\",\"backup\":\"%s\"}\n",
                     package->id, hash);
    if (n < 0 || n >= (int)sizeof(text) ||
        pos_fs_write_new(journal, text, (size_t)n))
      return failure(error,
                     "Cannot persist reinstall journal; old POS unchanged");
  }
  if (resume && !previous &&
      pos_backup_matches_progress(root, backup, platform->progress,
                                  platform->ctx, error))
    return -1;
  if (platform->report)
    platform->report(platform->ctx, "reinstall_journaled", hash);
  if (!previous && rename(current, old))
    return failure(error, "Cannot retire old POS tree; originals preserved");
  if (platform->report)
    platform->report(platform->ctx, "reinstall_retired", hash);
  /* New installation can now use the normal transaction, retaining old tree +
   * ZIP. */
  return 0;
}
