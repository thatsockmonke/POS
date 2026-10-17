#include "cJSON.h"
#include "pos_update.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  char id[65], before[65], after[65], old_version[32], new_version[32];
  uint32_t sequence;
  bool firmware;
} transaction_t;
typedef struct {
  char stage[POS_FS_PATH], current[POS_FS_PATH], old[POS_FS_PATH],
      next[POS_FS_PATH], journal[POS_FS_PATH], path[POS_FS_PATH],
      other[POS_FS_PATH];
} paths_t;
static int fail(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static bool exists(const char *p) {
  struct stat st;
  return !pos_fs_lstat(p, &st);
}
static int directory(const char *full) {
  if (strlen(full) >= POS_FS_PATH)
    return -1;
  char p[POS_FS_PATH];
  strcpy(p, full);
  for (char *s = p + 1;; ++s)
    if (*s == '/' || !*s) {
      char save = *s;
      *s = 0;
      struct stat st;
      if (pos_fs_lstat(p, &st)) {
        if (errno != ENOENT || mkdir(p, 0775))
          return -1;
      } else if (!S_ISDIR(st.st_mode))
        return -1;
      *s = save;
      if (!save)
        break;
    }
  return 0;
}
static int regular(const char *p) {
  struct stat st;
  return pos_fs_lstat(p, &st) || !S_ISREG(st.st_mode) ? -1 : 0;
}
static int verify(const char *path, const pos_file_t *file) {
  char hash[65];
  return regular(path) || pos_hash_file(path, file->size, hash) ||
                 strcmp(hash, file->sha256)
             ? -1
             : 0;
}
static int write_new(const char *path, const void *text, size_t n) {
  if (exists(path)) {
    char a[65], b[65];
    return regular(path) || pos_hash_data(text, n, a) ||
                   pos_hash_file(path, n, b) || strcmp(a, b)
               ? -1
               : 0;
  }
  if (pos_fs_write_new(path, text, n))
    return -1;
  char a[65], b[65];
  return pos_hash_data(text, n, a) || pos_hash_file(path, n, b) || strcmp(a, b)
             ? -1
             : 0;
}
static int owner(const char *root, const char *id) {
  char path[POS_FS_PATH], temp[POS_FS_PATH], hash[65];
  if (pos_fs_join(path, root, "POS/.pos-transaction"))
    return -1;
  if (!pos_hash_file(path, 64, hash)) {
    char expected[65];
    if (!pos_hash_data(id, 64, expected) && !strcmp(hash, expected))
      return 0;
  }
  for (unsigned i = 0; i < 100; ++i) {
    if (snprintf(temp, sizeof(temp), "%s.update-%u.part", path, i) >=
        (int)sizeof(temp))
      return -1;
    if (exists(temp))
      continue;
    if (write_new(temp, id, 64))
      return -1;
    if (unlink(path) && errno != ENOENT)
      return -1;
    return rename(temp, path) ? -1 : 0;
  }
  return -1;
}
static int set_paths(paths_t *p, const char *root, const char *id) {
  char relative[96];
  snprintf(relative, sizeof(relative), "POS/Updates/%s", id);
  return pos_fs_join(p->stage, root, relative) ||
                 pos_fs_join(p->current, root, "POS/System") ||
                 pos_fs_join(p->old, p->stage, "old-System") ||
                 pos_fs_join(p->next, p->stage, "new-System") ||
                 pos_fs_join(p->journal, root, POS_UPDATE_JOURNAL)
             ? -1
             : 0;
}
static int copy(const char *from, const char *to, const pos_file_t *file,
                const pos_platform_t *platform) {
  if (!verify(to, file))
    return 0;
  if (exists(to) || regular(from))
    return -1;
  char temporary[POS_FS_PATH];
  FILE *out = NULL;
  for (unsigned i = 0; i < 100; ++i) {
    if (snprintf(temporary, sizeof(temporary), "%s.copy-%u.part", to, i) >=
        (int)sizeof(temporary))
      return -1;
    out = fopen(temporary, "wbx");
    if (out)
      break;
    if (errno != EEXIST)
      return -1;
  }
  FILE *in = fopen(from, "rb");
  unsigned char *buffer = malloc(4096);
  int bad = !in || !out || !buffer;
  uint64_t total = 0;
  size_t n;
  while (!bad && (n = fread(buffer, 1, 4096, in))) {
    total += n;
    if (total > file->size || fwrite(buffer, 1, n, out) != n ||
        (platform->progress &&
         platform->progress(platform->ctx, "update-copy", file->path, total,
                            file->size)))
      bad = 1;
  }
  if (in) {
    if (ferror(in))
      bad = 1;
    fclose(in);
  }
  free(buffer);
  if (out) {
    if (fflush(out) || fsync(fileno(out)))
      bad = 1;
    if (fclose(out))
      bad = 1;
  }
  return bad || verify(temporary, file) || pos_fs_publish_new(temporary, to)
             ? -1
             : 0;
}
static int tree(const char *system, const char *expected, pos_package_t **out,
                pos_error_t *e) {
  char manifest[POS_FS_PATH];
  pos_package_t *p = NULL;
  if (pos_fs_join(manifest, system, "installed.pos") ||
      pos_load_manifest(manifest, &p, e))
    return -1;
  int result = strcmp(p->id, expected) ? -1 : 0;
  for (size_t i = 0; !result && i < p->count; ++i) {
    if (pos_fs_join(manifest, system, p->files[i].path + 7) ||
        verify(manifest, &p->files[i]))
      result = -1;
  }
  if (out && !result)
    *out = p;
  else
    pos_package_free(p);
  return result;
}
static bool hexid(const cJSON *j, const char *name, char out[65]) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, name);
  if (!cJSON_IsString(v) || strlen(v->valuestring) != 64 ||
      strspn(v->valuestring, "0123456789abcdef") != 64)
    return false;
  strcpy(out, v->valuestring);
  return true;
}
static int load_transaction(const char *root, transaction_t *t,
                            pos_error_t *e) {
  char file[POS_FS_PATH], text[1025];
  if (pos_fs_join(file, root, POS_UPDATE_JOURNAL))
    return -1;
  FILE *f = fopen(file, "rb");
  if (!f)
    return errno == ENOENT
               ? 1
               : fail(e, "Update journal unreadable; preserve SD contents.");
  size_t n = fread(text, 1, sizeof(text), f);
  int bad = ferror(f);
  fclose(f);
  if (bad || n >= sizeof(text) || !pos_fs_json_safe(text, n))
    return fail(e,
                "Interrupted/unknown update journal preserved. Use recovery.");
  text[n] = 0;
  cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *seq = cJSON_GetObjectItemCaseSensitive(j, "sequence"),
        *fw = cJSON_GetObjectItemCaseSensitive(j, "firmware"),
        *a = cJSON_GetObjectItemCaseSensitive(j, "old_version"),
        *b = cJSON_GetObjectItemCaseSensitive(j, "new_version"),
        *checksum = cJSON_GetObjectItemCaseSensitive(j, "checksum");
  char *body = NULL;
  char hash[65];
  int compare;
  bad = !cJSON_IsObject(j) || cJSON_GetArraySize(j) != 9 ||
        !cJSON_IsNumber(v) || v->valuedouble != 1 || !hexid(j, "id", t->id) ||
        !hexid(j, "before", t->before) || !hexid(j, "after", t->after) ||
        !cJSON_IsNumber(seq) || seq->valuedouble < 1 ||
        seq->valuedouble > UINT32_MAX ||
        seq->valuedouble != (uint32_t)seq->valuedouble || !cJSON_IsBool(fw) ||
        !cJSON_IsString(a) || !cJSON_IsString(b) ||
        pos_version_compare(a->valuestring, b->valuestring, &compare) ||
        !cJSON_IsString(checksum) || strlen(checksum->valuestring) != 64;
  if (!bad) {
    char saved[65];
    strcpy(saved, checksum->valuestring);
    cJSON_DeleteItemFromObjectCaseSensitive(j, "checksum");
    body = cJSON_PrintUnformatted(j);
    bad =
        !body || pos_hash_data(body, strlen(body), hash) || strcmp(saved, hash);
  }
  if (!bad) {
    t->sequence = seq->valuedouble;
    t->firmware = cJSON_IsTrue(fw);
    strcpy(t->old_version, a->valuestring);
    strcpy(t->new_version, b->valuestring);
  }
  free(body);
  cJSON_Delete(j);
  return bad ? fail(e, "Invalid update journal preserved. Use recovery.") : 0;
}
static int journal(paths_t *p, const transaction_t *t) {
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddStringToObject(j, "id", t->id);
  cJSON_AddStringToObject(j, "before", t->before);
  cJSON_AddStringToObject(j, "after", t->after);
  cJSON_AddNumberToObject(j, "sequence", t->sequence);
  cJSON_AddBoolToObject(j, "firmware", t->firmware);
  cJSON_AddStringToObject(j, "old_version", t->old_version);
  cJSON_AddStringToObject(j, "new_version", t->new_version);
  char *body = cJSON_PrintUnformatted(j);
  char hash[65];
  int result = -1;
  if (!body || pos_hash_data(body, strlen(body), hash))
    goto done;
  cJSON_AddStringToObject(j, "checksum", hash);
  free(body);
  body = cJSON_PrintUnformatted(j);
  if (!body || snprintf(p->other, sizeof(p->other), "%s.part", p->journal) >=
                   (int)sizeof(p->other))
    goto done;
  if (write_new(p->other, body, strlen(body)) ||
      pos_fs_publish_new(p->other, p->journal))
    goto done;
  result = 0;
done:
  free(body);
  cJSON_Delete(j);
  return result;
}
int pos_update_apply(const char *root, const pos_update_t *u,
                     const pos_update_platform_t *hooks, pos_error_t *e) {
  if (!u || !u->package || !hooks || !hooks->fetch ||
      !hooks->platform.free_bytes)
    return fail(e, "Update service unavailable.");
  paths_t *p = calloc(1, sizeof(*p));
  pos_package_t *before = NULL;
  int result = -1;
  if (!p)
    return fail(e, "Not enough memory for update paths.");
  if (set_paths(p, root, u->id))
    goto done;
  if (exists(p->journal)) {
    fail(e, "A pending update must boot or recover before another update.");
    goto done;
  }
  if (directory(p->current) ||
      pos_fs_join(p->path, p->current, "installed.pos") ||
      pos_load_manifest(p->path, &before, e) ||
      pos_validate_system(root, before, e))
    goto done;
  if (!u->firmware &&
      (strcmp(before->version, u->package->version) ||
       before->firmware.size != u->package->firmware.size ||
       strcmp(before->firmware.sha256, u->package->firmware.sha256))) {
    fail(e, "Asset update does not match the installed resident firmware.");
    goto done;
  }
  uint64_t required = 4 * 1024 * 1024ULL, available;
  for (size_t i = 0; i < u->package->count; ++i)
    required += u->package->files[i].size + 8 * 65536;
  if (u->firmware)
    required += u->package->firmware.size;
  if (hooks->platform.free_bytes(hooks->platform.ctx, root, &available) ||
      available < required) {
    fail(e, "Insufficient free SD space for a verified update and preserved "
            "previous System.");
    goto done;
  }
  if (directory(p->stage) || exists(p->old) || directory(p->next)) {
    fail(e, "Previous/unknown update work is preserved. Inspect Updates before "
            "retrying.");
    goto done;
  }
  const char *dirs[] = {"Apps", "Assets", "Fonts", "Sounds", "Config"};
  for (unsigned i = 0; i < 5; ++i)
    if (pos_fs_join(p->path, p->next, dirs[i]) || directory(p->path))
      goto done;
  if (pos_fs_join(p->path, p->stage, "old.pos") ||
      write_new(p->path, before->manifest, before->manifest_size))
    goto done;
  for (size_t i = 0; i < u->package->count; ++i) {
    const pos_file_t *file = &u->package->files[i];
    if (pos_fs_join(p->path, p->next, file->path + 7))
      goto done;
    char *slash = strrchr(p->path, '/');
    *slash = 0;
    if (directory(p->path))
      goto done;
    *slash = '/';
    if (!verify(p->path, file))
      continue;
    if (exists(p->path)) {
      fail(e, "A changed or incomplete staged file was preserved. Use a fresh "
              "signed update sequence.");
      goto done;
    }
    if (snprintf(p->other, sizeof(p->other), "%s/POS/%s", root, file->path) >=
        (int)sizeof(p->other))
      goto done;
    if (!verify(p->other, file)) {
      if (copy(p->other, p->path, file, &hooks->platform))
        goto done;
    } else {
      char url[512];
      if (snprintf(url, sizeof(url), "%s%spayload/%s", u->base_url,
                   u->base_url[strlen(u->base_url) - 1] == '/' ? "" : "/",
                   file->path) >= (int)sizeof(url))
        goto done;
      *slash = 0;
      int fetched = hooks->fetch(hooks->ctx, url, p->path, slash + 1,
                                 file->size, file->sha256, e);
      *slash = '/';
      if (fetched || verify(p->path, file))
        goto done;
    }
  }
  if (u->signed_manifest && (pos_fs_join(p->path,p->next,"signed-update.json") ||
      write_new(p->path,u->signed_manifest,strlen(u->signed_manifest)))) goto done;
  if (pos_fs_join(p->path, p->next, "installed.pos") ||
      write_new(p->path, u->package->manifest, u->package->manifest_size) ||
      tree(p->next, u->package->id, NULL, e))
    goto done;
  if (u->firmware) {
    char url[512];
    if (!hooks->platform.write_firmware || !hooks->platform.select_firmware ||
        pos_fs_join(p->path, p->stage, "firmware.bin") ||
        snprintf(url, sizeof(url), "%s%sfirmware.bin", u->base_url,
                 u->base_url[strlen(u->base_url) - 1] == '/' ? "" : "/") >=
            (int)sizeof(url))
      goto done;
    if (verify(p->path, &u->package->firmware)) {
      if (exists(p->path) ||
          hooks->fetch(hooks->ctx, url, p->stage, "firmware.bin",
                       u->package->firmware.size, u->package->firmware.sha256,
                       e) ||
          verify(p->path, &u->package->firmware))
        goto done;
    }
    if (hooks->platform.write_firmware(hooks->platform.ctx, p->path,
                                       u->package)) {
      fail(e, "Inactive firmware write/readback failed. Current System and "
              "firmware retained.");
      goto done;
    }
  }
  if (hooks->platform.progress &&
      hooks->platform.progress(hooks->platform.ctx, "update-ready", u->release,
                               1, 1)) {
    fail(e, "Update cancelled before commit. Current System retained.");
    goto done;
  }
  transaction_t t = {.sequence = u->sequence, .firmware = u->firmware};
  strcpy(t.id, u->id);
  strcpy(t.before, before->id);
  strcpy(t.after, u->package->id);
  strcpy(t.old_version, before->version);
  strcpy(t.new_version, u->package->version);
  if (journal(p, &t)) {
    fail(e, "Could not persist update journal; current System unchanged.");
    goto done;
  }
  if (hooks->platform.report)
    hooks->platform.report(hooks->platform.ctx, "update-journaled", u->id);
  if (rename(p->current, p->old))
    goto rollback;
  if (hooks->platform.report)
    hooks->platform.report(hooks->platform.ctx, "update-retired", u->id);
  if (rename(p->next, p->current) || owner(root, u->package->id))
    goto rollback;
  if (hooks->platform.report)
    hooks->platform.report(hooks->platform.ctx, "update-swapped", u->id);
  if (u->firmware && hooks->platform.select_firmware(hooks->platform.ctx))
    goto rollback;
  if (!u->firmware) {
    if (pos_fs_join(p->path, p->stage, "assets-committed") ||
        write_new(p->path, u->package->id, 64))
      goto rollback;
  }
  result = 0;
  goto done;
rollback: {
  pos_error_t recovery = {0};
  pos_update_recover(root, before->version, &recovery);
  fail(e, "Update interrupted; previous System retained/restored. Inspect "
          "recovery if required.");
}
done:
  if (result && e && !*e->message)
    fail(e, "Update could not complete. Current data and update work are "
            "preserved.");
  pos_package_free(before);
  free(p);
  return result;
}
int pos_update_recover(const char *root, const char *running, pos_error_t *e) {
  transaction_t t;
  int loaded = load_transaction(root, &t, e);
  if (loaded)
    return loaded == 1 ? 0 : -1;
  paths_t *p = calloc(1, sizeof(*p));
  if (!p)
    return fail(e, "Not enough memory for update recovery.");
  int result = -1;
  if (set_paths(p, root, t.id))
    goto done;
  bool is_new = !strcmp(running, t.new_version);
  bool accepted = t.firmware && is_new;
  if (!t.firmware) {
    if (pos_fs_join(p->path, p->stage, "assets-committed"))
      goto done;
    char hash[65], expected[65];
    accepted = !pos_hash_file(p->path, 64, hash) &&
               !pos_hash_data(t.after, 64, expected) && !strcmp(hash, expected);
  }
  if (accepted && !tree(p->current, t.after, NULL, e)) {
    if (owner(root, t.after))
      goto done;
    result = 1;
    goto done;
  }
  if (exists(p->old)) {
    if (tree(p->old, t.before, NULL, e))
      goto done;
    if (exists(p->current)) {
      if (pos_fs_join(p->path, p->stage, "failed-System") || exists(p->path) ||
          rename(p->current, p->path))
        goto done;
    }
    if (rename(p->old, p->current))
      goto done;
  }
  if (tree(p->current, t.before, NULL, e) || owner(root, t.before) ||
      pos_fs_join(p->path, p->stage, "rolled-back.json") ||
      rename(p->journal, p->path))
    goto done;
  result = t.firmware && is_new ? 2 : 0;
done:
  free(p);
  return result < 0
             ? fail(e,
                    "Interrupted update could not be resolved automatically. "
                    "Previous System and staging are preserved; use recovery.")
             : result;
}
int pos_update_confirm(const char *root, uint32_t *sequence, pos_error_t *e) {
  transaction_t t;
  int loaded = load_transaction(root, &t, e);
  if (loaded)
    return loaded == 1 ? 0 : -1;
  paths_t *p = calloc(1, sizeof(*p));
  if (!p)
    return -1;
  int result = -1;
  if (set_paths(p, root, t.id) || tree(p->current, t.after, NULL, e) ||
      owner(root, t.after) || pos_fs_join(p->path, p->stage, "complete.json"))
    goto done;
  if (rename(p->journal, p->path))
    goto done;
  if (sequence)
    *sequence = t.sequence;
  result = 0;
done:
  free(p);
  return result
             ? fail(
                   e,
                   "Update confirmation pending; previous System is preserved.")
             : 0;
}

int pos_update_pending_sequence(const char *root, uint32_t *sequence,
                                pos_error_t *e) {
  transaction_t t;
  int result = load_transaction(root, &t, e);
  if (!result && sequence)
    *sequence = t.sequence;
  return result;
}
