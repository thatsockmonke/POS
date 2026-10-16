#include "pos_editor.h"
#include "cJSON.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

enum { BUFFER = 4096 };
static int fail(pos_error_t *error, const char *message) {
  if (error)
    snprintf(error->message, sizeof(error->message), "%s", message);
  return -1;
}
static bool user_path(const char *path) {
  return pos_fs_archive_path(path) && pos_fs_user_path(path) &&
         strncasecmp(path, "POS/Users/settings.", 19);
}
static int path(const pos_editor_t *e, const char *name,
                char out[POS_FS_PATH]) {
  return pos_fs_join(out, e->directory, name);
}
static int flush(FILE *f) { return fflush(f) || fsync(fileno(f)) ? -1 : 0; }
static int copy(FILE *in, FILE *out, uint32_t *size, uint32_t *crc,
                pos_io_progress_t progress, void *ctx) {
  unsigned char *buffer = malloc(BUFFER);
  if (!buffer)
    return -1;
  size_t n;
  *size = *crc = 0;
  int bad = 0;
  while ((n = fread(buffer, 1, BUFFER, in))) {
    if (memchr(buffer, 0, n) || *size > INT32_MAX - n ||
        fwrite(buffer, 1, n, out) != n) {
      bad = 1;
      break;
    }
    *size += n;
    *crc = pos_crc32(*crc, buffer, n);
    if (progress && progress(ctx, "Preparing editor", *size, 0)) {
      bad = 1;
      break;
    }
  }
  free(buffer);
  return bad || ferror(in) || flush(out) ? -1 : 0;
}
static FILE *owned_file(const char *name, const char *mode) {
  struct stat st;
  if (!pos_fs_lstat(name, &st)) {
    if (!S_ISREG(st.st_mode))
      return NULL;
  } else if (errno != ENOENT)
    return NULL;
  return fopen(name, mode);
}
static FILE *index_file(const pos_editor_t *e, unsigned slot,
                        const char *mode) {
  char name[32], full[POS_FS_PATH];
  snprintf(name, sizeof(name), "pieces.%u.csv", slot);
  return path(e, name, full) ? NULL : owned_file(full, mode);
}
/* Each row references immutable base.txt or append-only added.txt. */
static int piece(FILE *f, char *kind, uint32_t *offset, uint32_t *length) {
  char row[96], tail;
  if (!fgets(row, sizeof(row), f))
    return ferror(f) ? -1 : 0;
  unsigned long a, b;
  if (sscanf(row, "%c,%lu,%lu%c", kind, &a, &b, &tail) != 4 || tail != '\n' ||
      (*kind != 'O' && *kind != 'A') || !b || a > INT32_MAX || b > INT32_MAX ||
      a + b > INT32_MAX)
    return -1;
  *offset = a;
  *length = b;
  return 1;
}
static int prefix_crc_io(const char *filename, uint32_t size, uint32_t *crc,
                         pos_io_progress_t progress, void *ctx) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return -1;
  unsigned char *buffer = malloc(BUFFER);
  if (!buffer) {
    fclose(f);
    return -1;
  }
  uint32_t remaining = size;
  *crc = 0;
  int bad = 0;
  while (remaining) {
    size_t n = remaining < BUFFER ? remaining : BUFFER;
    if (fread(buffer, 1, n, f) != n) {
      bad = 1;
      break;
    }
    *crc = pos_crc32(*crc, buffer, n);
    remaining -= n;
    if (progress &&
        progress(ctx, "Verifying working text", size - remaining, size)) {
      bad = 1;
      break;
    }
  }
  if (ferror(f))
    bad = 1;
  fclose(f);
  free(buffer);
  return bad ? -1 : 0;
}
static int prefix_crc(const char *filename, uint32_t size, uint32_t *crc) {
  return prefix_crc_io(filename, size, crc, NULL, NULL);
}
static int validate_text_io(const pos_editor_t *e, pos_io_progress_t progress,
                            void *ctx) {
  char full[POS_FS_PATH];
  uint32_t size, crc;
  if (path(e, "base.txt", full) || pos_fs_file_size(full, &size) ||
      size != e->base_size || prefix_crc_io(full, size, &crc, progress, ctx) ||
      crc != e->base_crc)
    return -1;
  if (path(e, "added.txt", full) || pos_fs_file_size(full, &size) ||
      size < e->added_size ||
      prefix_crc_io(full, e->added_size, &crc, progress, ctx) ||
      crc != e->added_crc)
    return -1;
  return 0;
}
static int validate_text(const pos_editor_t *e) {
  return validate_text_io(e, NULL, NULL);
}
typedef struct {
  pos_io_progress_t progress;
  void *ctx;
  const char *phase;
} crc_context_t;
static int crc_progress(void *opaque, uint64_t done, uint64_t total) {
  crc_context_t *c = opaque;
  return c->progress ? c->progress(c->ctx, c->phase, done, total) : 0;
}
static int verify_file(const char *file, uint32_t size, uint32_t expected,
                       const char *phase, pos_io_progress_t progress,
                       void *ctx) {
  crc_context_t context = {progress, ctx, phase};
  uint32_t actual;
  return pos_fs_crc_file_progress(file, size, &actual, crc_progress,
                                  &context) ||
                 actual != expected
             ? -1
             : 0;
}
static int validate_index(const pos_editor_t *e, unsigned slot,
                          uint32_t expected, uint32_t *crc) {
  char full[POS_FS_PATH], name[32];
  uint32_t base, added, bytes;
  if (path(e, "base.txt", full) || pos_fs_file_size(full, &base) ||
      path(e, "added.txt", full) || pos_fs_file_size(full, &added))
    return -1;
  FILE *f = index_file(e, slot, "rb");
  if (!f)
    return -1;
  uint64_t total = 0;
  char kind;
  uint32_t start, count;
  int result;
  while ((result = piece(f, &kind, &start, &count)) == 1) {
    if ((uint64_t)start + count > (kind == 'O' ? base : added)) {
      result = -1;
      break;
    }
    total += count;
    if (total > INT32_MAX) {
      result = -1;
      break;
    }
  }
  fclose(f);
  snprintf(name, sizeof(name), "pieces.%u.csv", slot);
  return result || total != expected || path(e, name, full) ||
                 pos_fs_file_size(full, &bytes) ||
                 pos_fs_crc_file(full, bytes, crc)
             ? -1
             : 0;
}
static int checkpoint(pos_editor_t *e, unsigned slot, uint32_t length,
                      uint32_t cursor) {
  uint32_t crc;
  if (e->generation == UINT32_MAX || validate_index(e, slot, length, &crc))
    return -1;
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return -1;
  cJSON_AddNumberToObject(j, "format", 1);
  cJSON_AddStringToObject(j, "source", e->source);
  cJSON_AddNumberToObject(j, "generation", e->generation + 1);
  cJSON_AddNumberToObject(j, "length", length);
  cJSON_AddNumberToObject(j, "cursor", cursor);
  cJSON_AddNumberToObject(j, "original_size", e->original_size);
  cJSON_AddNumberToObject(j, "original_crc", e->original_crc);
  cJSON_AddBoolToObject(j, "original_exists", e->original_exists);
  cJSON_AddNumberToObject(j, "index_crc", crc);
  cJSON_AddNumberToObject(j, "base_size", e->base_size);
  cJSON_AddNumberToObject(j, "base_crc", e->base_crc);
  cJSON_AddNumberToObject(j, "added_size", e->added_size);
  cJSON_AddNumberToObject(j, "added_crc", e->added_crc);
  cJSON_AddNumberToObject(j, "slot", slot);
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
  char name[32], full[POS_FS_PATH], temporary[POS_FS_PATH];
  snprintf(name, sizeof(name), "state.%u.json", slot);
  int result = -1;
  if (path(e, name, full) || path(e, "state.part", temporary))
    goto done;
  if (unlink(temporary) && errno != ENOENT)
    goto done;
  if (pos_fs_write_new(temporary, text, strlen(text)))
    goto done;
  if (unlink(full) && errno != ENOENT)
    goto done;
  if (rename(temporary, full))
    goto done;
  e->slot = slot;
  e->length = length;
  e->cursor = cursor;
  ++e->generation;
  result = 0;
done:
  free(text);
  return result;
}
void pos_editor_discard(pos_editor_t *e) {
  if (!*e->directory)
    return;
  const char *owned[] = {"base.txt",     "added.txt",     "pieces.0.csv",
                         "pieces.1.csv", "state.0.json",  "state.1.json",
                         "state.part",   "clipboard.txt", "clipboard.part"};
  char full[POS_FS_PATH];
  for (unsigned i = 0; i < sizeof(owned) / sizeof(*owned); ++i)
    if (!path(e, owned[i], full))
      unlink(full);
  for (unsigned i = 0; i < 32; ++i)
    for (unsigned redo = 0; redo < 2; ++redo) {
      char name[32];
      snprintf(name, sizeof(name), "%s.%u.csv", redo ? "redo" : "undo", i);
      if (!path(e, name, full))
        unlink(full);
    }
  rmdir(e->directory);
  *e->directory = 0;
}
int pos_editor_open(pos_editor_t *e, const char *root, const char *relative,
                    pos_io_progress_t progress, void *ctx, pos_error_t *error) {
  memset(e, 0, sizeof(*e));
  if (strlen(root) >= sizeof(e->root) || !user_path(relative))
    return fail(error, "Choose a user document path.");
  strcpy(e->root, root);
  strcpy(e->source, relative);
  char full[POS_FS_PATH], parent[POS_FS_PATH];
  if (pos_fs_join(full, root, relative))
    return -1;
  struct stat st;
  if (!pos_fs_lstat(full, &st)) {
    if (!S_ISREG(st.st_mode))
      return fail(error, "Choose a regular document.");
    e->original_exists = true;
    uint32_t bytes;
    if (pos_fs_file_size(full, &bytes) || bytes > INT32_MAX)
      return fail(error, "This filesystem supports editor seeks through 2 GiB "
                         "minus one byte.");
  } else if (errno != ENOENT)
    return fail(error, "Cannot inspect the document.");
  if (pos_fs_join(parent, root, POS_EDITOR_DRAFT_ROOT) || pos_fs_mkdirs(parent))
    return fail(error, "Cannot create editor workspace.");
  bool created = false;
  for (unsigned i = 0; i < 10000; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "editor-%u", i);
    if (pos_fs_join(e->directory, parent, name))
      break;
    if (!mkdir(e->directory, 0700)) {
      created = true;
      break;
    }
    if (errno != EEXIST)
      break;
  }
  if (!created) {
    *e->directory = 0;
    return fail(error, "No free editor workspace. Old drafts were preserved.");
  }
  FILE *in = e->original_exists ? fopen(full, "rb") : NULL;
  char base[POS_FS_PATH];
  path(e, "base.txt", base);
  FILE *out = fopen(base, "wbx");
  int bad = !out || (e->original_exists && !in);
  if (!bad && in)
    bad = copy(in, out, &e->original_size, &e->original_crc, progress, ctx);
  if (in)
    fclose(in);
  if (out && fclose(out))
    bad = 1;
  path(e, "added.txt", base);
  if (!bad && pos_fs_write_new(base, "", 0))
    bad = 1;
  out = !bad ? index_file(e, 0, "wbx") : NULL;
  if (!out)
    bad = 1;
  if (out) {
    if (e->original_size &&
        fprintf(out, "O,0,%u\n", (unsigned)e->original_size) < 0)
      bad = 1;
    if (flush(out) || fclose(out))
      bad = 1;
  }
  e->base_size = e->original_size;
  e->base_crc = e->original_crc;
  if (!bad)
    bad = validate_text(e);
  if (!bad)
    bad = checkpoint(e, 0, e->original_size, 0);
  if (bad) {
    pos_editor_discard(e);
    return fail(
        error,
        "Editor preparation failed or was cancelled. Original preserved.");
  }
  return 0;
}
static int load_state(pos_editor_t *e, const char *root, const char *folder,
                      unsigned slot) {
  memset(e, 0, sizeof(*e));
  if (strlen(root) >= sizeof(e->root) ||
      strncmp(folder, POS_EDITOR_DRAFT_ROOT "/editor-",
              sizeof(POS_EDITOR_DRAFT_ROOT "/editor-") - 1) ||
      strchr(folder + sizeof(POS_EDITOR_DRAFT_ROOT "/editor-") - 1, '/') ||
      !folder[sizeof(POS_EDITOR_DRAFT_ROOT "/editor-") - 1])
    return -1;
  strcpy(e->root, root);
  if (pos_fs_join(e->directory, root, folder))
    return -1;
  struct stat workspace;
  if (pos_fs_lstat(e->directory, &workspace) || !S_ISDIR(workspace.st_mode))
    return -1;
  char name[32], full[POS_FS_PATH], text[2049];
  snprintf(name, sizeof(name), "state.%u.json", slot);
  if (path(e, name, full))
    return -1;
  FILE *f = fopen(full, "rb");
  if (!f)
    return -1;
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  int bad = ferror(f) || !feof(f);
  fclose(f);
  text[n] = 0;
  if (bad || !pos_fs_json_safe(text, n))
    return -1;
  cJSON *envelope = cJSON_ParseWithOpts(text, NULL, true);
  cJSON *body = cJSON_GetObjectItemCaseSensitive(envelope, "payload");
  cJSON *hash = cJSON_GetObjectItemCaseSensitive(envelope, "sha256");
  char digest[65];
  if (!cJSON_IsString(body) || !cJSON_IsString(hash) ||
      pos_hash_data(body->valuestring, strlen(body->valuestring), digest) ||
      strcmp(digest, hash->valuestring) ||
      !pos_fs_json_safe(body->valuestring, strlen(body->valuestring))) {
    cJSON_Delete(envelope);
    return -1;
  }
  cJSON *j = cJSON_ParseWithOpts(body->valuestring, NULL, true);
  cJSON *source = cJSON_GetObjectItemCaseSensitive(j, "source"),
        *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *exists = cJSON_GetObjectItemCaseSensitive(j, "original_exists");
  const char *names[] = {"length",        "cursor",       "generation",
                         "original_size", "original_crc", "index_crc",
                         "slot",          "base_size",    "base_crc",
                         "added_size",    "added_crc"};
  uint32_t values[11];
  bad = !cJSON_IsNumber(format) || format->valuedouble != 1 ||
        !cJSON_IsString(source) || !user_path(source->valuestring) ||
        !cJSON_IsBool(exists);
  for (unsigned i = 0; i < 11; ++i) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, names[i]);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 ||
        v->valuedouble > UINT32_MAX ||
        v->valuedouble != (uint32_t)v->valuedouble)
      bad = 1;
    else
      values[i] = (uint32_t)v->valuedouble;
  }
  if (!bad) {
    strcpy(e->source, source->valuestring);
    e->length = values[0];
    e->cursor = values[1];
    e->generation = values[2];
    e->original_size = values[3];
    e->original_crc = values[4];
    e->original_exists = cJSON_IsTrue(exists);
    e->base_size = values[7];
    e->base_crc = values[8];
    e->added_size = values[9];
    e->added_crc = values[10];
    e->slot = slot;
    e->dirty = true;
    uint32_t crc;
    bad = values[6] != slot || !e->generation || e->length > INT32_MAX ||
          e->cursor > e->length || validate_index(e, slot, e->length, &crc) ||
          crc != values[5] || validate_text(e);
  }
  cJSON_Delete(j);
  cJSON_Delete(envelope);
  return bad ? -1 : 0;
}
int pos_editor_recover(pos_editor_t *e, const char *root, const char *folder,
                       pos_error_t *error) {
  pos_editor_t *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
  if (!a || !b) {
    free(a);
    free(b);
    return -1;
  }
  int ra = load_state(a, root, folder, 0), rb = load_state(b, root, folder, 1);
  if (!ra || !rb)
    *e = !ra && (rb || a->generation >= b->generation) ? *a : *b;
  free(a);
  free(b);
  return ra && rb ? fail(error, "No verified draft checkpoint. Working files "
                                "were preserved.")
                  : 0;
}
int pos_editor_read(const pos_editor_t *e, uint32_t offset, void *buffer,
                    size_t capacity, size_t *read) {
  *read = 0;
  if (offset > e->length)
    return -1;
  FILE *index = index_file(e, e->slot, "rb");
  if (!index)
    return -1;
  uint32_t position = 0, start, count;
  char kind;
  int result;
  while (*read < capacity &&
         (result = piece(index, &kind, &start, &count)) == 1) {
    if (offset >= position + count) {
      position += count;
      continue;
    }
    uint32_t local = offset > position ? offset - position : 0;
    size_t n = count - local;
    if (n > capacity - *read)
      n = capacity - *read;
    char full[POS_FS_PATH];
    path(e, kind == 'O' ? "base.txt" : "added.txt", full);
    FILE *data = fopen(full, "rb");
    if (!data || fseek(data, start + local, SEEK_SET) ||
        fread((char *)buffer + *read, 1, n, data) != n) {
      if (data)
        fclose(data);
      fclose(index);
      return -1;
    }
    fclose(data);
    *read += n;
    offset += n;
    position += count;
  }
  int bad = ferror(index);
  fclose(index);
  return bad || (offset < e->length && *read < capacity) ? -1 : 0;
}
typedef struct {
  FILE *f;
  char kind;
  uint32_t start, count;
} writer_t;
static int emit(writer_t *w, char kind, uint32_t start, uint32_t count) {
  if (!count)
    return 0;
  if (w->count && w->kind == kind && w->start + w->count == start) {
    w->count += count;
    return 0;
  }
  if (w->count && fprintf(w->f, "%c,%u,%u\n", w->kind, (unsigned)w->start,
                          (unsigned)w->count) < 0)
    return -1;
  w->kind = kind;
  w->start = start;
  w->count = count;
  return 0;
}
enum { HISTORY = 32 };
static int history_path(const pos_editor_t *e, bool redo, unsigned slot,
                        char full[POS_FS_PATH]) {
  char name[40];
  snprintf(name, sizeof(name), "%s.%u.csv", redo ? "redo" : "undo",
           slot % HISTORY);
  return path(e, name, full);
}
static int history_snapshot(const pos_editor_t *e, bool redo, unsigned slot) {
  char full[POS_FS_PATH];
  if (history_path(e, redo, slot, full))
    return -1;
  FILE *in = index_file(e, e->slot, "rb"), *out = owned_file(full, "wb");
  unsigned char *buffer = malloc(BUFFER);
  uint32_t crc = 0, size = 0;
  int bad = !in || !out || !buffer;
  if (!bad) {
    size_t n;
    while ((n = fread(buffer, 1, BUFFER, in))) {
      crc = pos_crc32(crc, buffer, n);
      size += n;
    }
    bad = ferror(in) || fseek(in, 0, SEEK_SET);
    if (!bad &&
        fprintf(out, "POS-EDITOR-HISTORY,1,%u,%u,%u,%u\n", (unsigned)e->length,
                (unsigned)e->cursor, (unsigned)size, (unsigned)crc) < 0)
      bad = 1;
    while (!bad && (n = fread(buffer, 1, BUFFER, in)))
      if (fwrite(buffer, 1, n, out) != n)
        bad = 1;
    if (ferror(in) || flush(out))
      bad = 1;
  }
  if (in)
    fclose(in);
  if (out && fclose(out))
    bad = 1;
  free(buffer);
  return bad ? -1 : 0;
}
int pos_editor_undo(pos_editor_t *e, bool redo, pos_error_t *error) {
  unsigned *count = redo ? &e->redo_count : &e->undo_count,
           *head = redo ? &e->redo_head : &e->undo_head;
  unsigned *other_count = redo ? &e->undo_count : &e->redo_count,
           *other_head = redo ? &e->undo_head : &e->redo_head;
  if (!*count)
    return fail(error, redo ? "Nothing to redo." : "Nothing to undo.");
  char full[POS_FS_PATH], header[160], tail;
  unsigned length, cursor, size, expected;
  unsigned target = (*head + HISTORY - 1) % HISTORY;
  if (history_path(e, redo, target, full))
    return -1;
  FILE *in = fopen(full, "rb");
  if (!in)
    return fail(error, "History unavailable; current draft retained.");
  if (!fgets(header, sizeof(header), in) ||
      sscanf(header, "POS-EDITOR-HISTORY,1,%u,%u,%u,%u%c", &length, &cursor,
             &size, &expected, &tail) != 5 ||
      tail != '\n' || length > INT32_MAX || cursor > length) {
    fclose(in);
    return fail(error, "Invalid history; current draft retained.");
  }
  unsigned char *buffer = malloc(BUFFER);
  unsigned slot = 1 - e->slot;
  FILE *out = buffer ? index_file(e, slot, "wb") : NULL;
  uint64_t bytes = 0;
  uint32_t crc = 0;
  int bad = !buffer || !out;
  size_t n;
  while (!bad && (n = fread(buffer, 1, BUFFER, in))) {
    bytes += n;
    crc = pos_crc32(crc, buffer, n);
    if (bytes > size || fwrite(buffer, 1, n, out) != n)
      bad = 1;
  }
  if (ferror(in) || bytes != size || crc != expected)
    bad = 1;
  fclose(in);
  free(buffer);
  if (out) {
    if (flush(out))
      bad = 1;
    if (fclose(out))
      bad = 1;
  }
  if (!bad)
    bad = history_snapshot(e, !redo, *other_head);
  if (!bad)
    bad = validate_text(e) || checkpoint(e, slot, length, cursor);
  if (bad)
    return fail(error, "Undo/redo failed; current checkpoint retained.");
  *head = target;
  --*count;
  *other_head = (*other_head + 1) % HISTORY;
  if (*other_count < HISTORY)
    ++*other_count;
  e->dirty = true;
  return 0;
}
static int replace_input(pos_editor_t *e, uint32_t remove, const char *text,
                         size_t length, FILE *input, pos_io_progress_t progress,
                         void *ctx, pos_error_t *error) {
  if (e->cursor > e->length || remove > e->length - e->cursor ||
      length > INT32_MAX - (e->length - remove))
    return fail(error, "Document exceeds the filesystem seek limit.");
  if (!remove && !length)
    return 0;
  if (history_snapshot(e, false, e->undo_head))
    return fail(error, "Cannot save undo history; edit not committed.");
  char added[POS_FS_PATH];
  path(e, "added.txt", added);
  uint32_t append;
  if (pos_fs_file_size(added, &append) || length > INT32_MAX - append)
    return fail(error, "Editor scratch space limit reached.");
  uint32_t old_size = e->added_size, old_crc = e->added_crc,
           new_crc = e->added_crc;
  if (append != e->added_size && prefix_crc(added, append, &new_crc))
    return -1;
  if (length) {
    FILE *f = owned_file(added, "ab");
    if (!f)
      return fail(error, "Cannot write editor scratch file.");
    int bad = 0;
    if (input) {
      unsigned char *buffer = malloc(BUFFER);
      uint32_t done = 0;
      if (!buffer)
        bad = 1;
      while (!bad && done < length) {
        size_t n = length - done;
        if (n > BUFFER)
          n = BUFFER;
        if (fread(buffer, 1, n, input) != n || fwrite(buffer, 1, n, f) != n) {
          bad = 1;
          break;
        }
        new_crc = pos_crc32(new_crc, buffer, n);
        done += n;
        if (progress && progress(ctx, "Pasting selection", done, length))
          bad = 1;
      }
      free(buffer);
    } else {
      bad = fwrite(text, 1, length, f) != length;
      new_crc = pos_crc32(new_crc, text, length);
    }
    if (flush(f))
      bad = 1;
    if (fclose(f))
      bad = 1;
    if (bad)
      return fail(error,
                  "Edit not committed. Previous draft remains available.");
  }
  unsigned slot = 1 - e->slot;
  FILE *old = index_file(e, e->slot, "rb"), *next = index_file(e, slot, "wb");
  if (!old || !next) {
    if (old)
      fclose(old);
    if (next)
      fclose(next);
    return -1;
  }
  writer_t w = {.f = next};
  uint32_t position = 0, start, count, end = e->cursor + remove;
  char kind;
  bool inserted = false;
  int status, bad = 0;
  while ((status = piece(old, &kind, &start, &count)) == 1 && !bad) {
    uint32_t last = position + count;
    if (position < e->cursor) {
      uint32_t before = e->cursor - position;
      if (before > count)
        before = count;
      bad |= emit(&w, kind, start, before);
    }
    if (!inserted && last >= e->cursor) {
      bad |= emit(&w, 'A', append, length);
      inserted = true;
    }
    if (last > end) {
      uint32_t skip = end > position ? end - position : 0;
      bad |= emit(&w, kind, start + skip, count - skip);
    }
    position = last;
  }
  if (!inserted)
    bad |= emit(&w, 'A', append, length);
  if (w.count && fprintf(next, "%c,%u,%u\n", w.kind, (unsigned)w.start,
                         (unsigned)w.count) < 0)
    bad = 1;
  if (status < 0 || ferror(old) || flush(next))
    bad = 1;
  fclose(old);
  if (fclose(next))
    bad = 1;
  e->added_size = append + length;
  e->added_crc = new_crc;
  if (!bad)
    bad = checkpoint(e, slot, e->length - remove + length, e->cursor + length);
  if (bad) {
    e->added_size = old_size;
    e->added_crc = old_crc;
  }
  if (bad)
    return fail(error, "Edit not committed. Previous draft remains available.");
  e->undo_head = (e->undo_head + 1) % HISTORY;
  if (e->undo_count < HISTORY)
    ++e->undo_count;
  e->redo_count = 0;
  e->dirty = true;
  return 0;
}
int pos_editor_replace(pos_editor_t *e, uint32_t remove, const char *text,
                       size_t length, pos_error_t *error) {
  if ((!text && length) || (length && memchr(text, 0, length)))
    return fail(error, "Text contains invalid NUL bytes.");
  return replace_input(e, remove, text, length, NULL, NULL, NULL, error);
}
int pos_editor_copy_selection(pos_editor_t *e, uint32_t start, uint32_t end,
                              pos_io_progress_t progress, void *ctx,
                              pos_error_t *error) {
  if (start >= end || end > e->length)
    return fail(error, "Select text first (Shift + arrows or Ctrl+A).");
  char full[POS_FS_PATH], temporary[POS_FS_PATH];
  if (path(e, "clipboard.txt", full) || path(e, "clipboard.part", temporary))
    return -1;
  FILE *out = owned_file(temporary, "wb");
  unsigned char *buffer = malloc(BUFFER);
  int bad = !out || !buffer;
  uint32_t done = 0, crc = 0;
  while (!bad && done < end - start) {
    size_t n, need = end - start - done;
    if (need > BUFFER)
      need = BUFFER;
    if (pos_editor_read(e, start + done, buffer, need, &n) || n != need ||
        fwrite(buffer, 1, n, out) != n) {
      bad = 1;
      break;
    }
    crc = pos_crc32(crc, buffer, n);
    done += n;
    if (progress && progress(ctx, "Copying selection", done, end - start))
      bad = 1;
  }
  free(buffer);
  if (out) {
    if (flush(out))
      bad = 1;
    if (fclose(out))
      bad = 1;
  }
  uint32_t check;
  if (!bad && (pos_fs_crc_file(temporary, done, &check) || check != crc))
    bad = 1;
  if (!bad && unlink(full) && errno != ENOENT)
    bad = 1;
  if (!bad && rename(temporary, full))
    bad = 1;
  return bad ? fail(error, "Clipboard incomplete; document untouched.") : 0;
}
int pos_editor_paste(pos_editor_t *e, uint32_t remove,
                     pos_io_progress_t progress, void *ctx,
                     pos_error_t *error) {
  char full[POS_FS_PATH];
  uint32_t bytes;
  if (path(e, "clipboard.txt", full) || pos_fs_file_size(full, &bytes) ||
      bytes > INT32_MAX)
    return fail(error, "Clipboard empty or unavailable.");
  FILE *in = fopen(full, "rb");
  if (!in)
    return fail(error, "Clipboard unavailable.");
  int result = replace_input(e, remove, NULL, bytes, in, progress, ctx, error);
  fclose(in);
  return result;
}
int pos_editor_save(pos_editor_t *e, const char *relative, bool replace,
                    pos_io_progress_t progress, void *ctx, pos_error_t *error) {
  if (!user_path(relative))
    return fail(error, "Choose a user document path.");
  if (validate_text_io(e, progress, ctx))
    return fail(
        error,
        "Working text integrity check failed. Original document preserved.");
  char target[POS_FS_PATH], staged[POS_FS_PATH], previous[POS_FS_PATH];
  if (pos_fs_join(target, e->root, relative))
    return -1;
  struct stat st;
  bool exists = !pos_fs_lstat(target, &st);
  if (exists) {
    uint32_t size;
    if (!replace || strcmp(relative, e->source) || !e->original_exists ||
        !S_ISREG(st.st_mode) || pos_fs_file_size(target, &size) ||
        size != e->original_size ||
        verify_file(target, size, e->original_crc, "Checking original document",
                    progress, ctx))
      return fail(error, "Destination exists or changed. Use Save As to "
                         "preserve both copies.");
  } else if (errno != ENOENT ||
             (replace && e->original_exists && !strcmp(relative, e->source)))
    return fail(error,
                "Original document changed or is unavailable. Use Save As.");
  FILE *out = NULL;
  for (unsigned i = 0; i < 100; ++i) {
    if (snprintf(staged, sizeof(staged), "%s.pos-save-%u.part", target, i) >=
        (int)sizeof(staged))
      break;
    out = fopen(staged, "wbx");
    if (out || errno != EEXIST)
      break;
  }
  if (!out)
    return fail(error, "Cannot create a new save staging file. Existing files "
                       "were preserved.");
  unsigned char *buffer = malloc(BUFFER);
  if (!buffer) {
    fclose(out);
    return fail(error,
                "Not enough memory to save. Original and draft preserved.");
  }
  uint32_t offset = 0, crc = 0;
  int bad = 0;
  while (offset < e->length && !bad) {
    size_t n;
    if (pos_editor_read(e, offset, buffer, BUFFER, &n) || !n ||
        fwrite(buffer, 1, n, out) != n) {
      bad = 1;
      break;
    }
    crc = pos_crc32(crc, buffer, n);
    offset += n;
    if (progress && progress(ctx, "Saving document", offset, e->length))
      bad = 1;
  }
  free(buffer);
  if (flush(out))
    bad = 1;
  if (fclose(out))
    bad = 1;
  if (!bad && verify_file(staged, e->length, crc, "Verifying saved document",
                          progress, ctx))
    bad = 1;
  if (bad)
    return fail(error,
                "Save incomplete. Original and working draft preserved.");
  if (exists) {
    uint32_t current_size;
    if (pos_fs_file_size(target, &current_size) ||
        current_size != e->original_size ||
        verify_file(target, current_size, e->original_crc,
                    "Rechecking original document", progress, ctx))
      return fail(error, "Original changed during save or verification was "
                         "cancelled. Verified staged copy retained.");
    bool chosen = false;
    for (unsigned i = 0; i < 10000; ++i) {
      if (snprintf(previous, sizeof(previous), "%s.previous-%u", target, i) >=
          (int)sizeof(previous))
        break;
      if (pos_fs_lstat(previous, &st) && errno == ENOENT) {
        chosen = true;
        break;
      }
    }
    if (!chosen || rename(target, previous))
      return fail(
          error,
          "Cannot preserve previous document; verified staged copy retained.");
  }
  if (pos_fs_publish_new(staged, target)) {
    if (exists)
      (void)rename(previous, target);
    return fail(error,
                "Could not publish save. Verified staged copy retained.");
  }
  strcpy(e->source, relative);
  e->original_exists = true;
  e->original_size = e->length;
  e->original_crc = crc;
  e->dirty = false;
  /* Save metadata without touching the active piece index. */
  if (checkpoint(e, e->slot, e->length, e->cursor))
    return fail(error, "Document saved; draft checkpoint could not be updated. "
                       "Keep the saved document.");
  return 0;
}
int pos_editor_find(pos_editor_t *e, const char *text, uint32_t start,
                    uint32_t *found, pos_io_progress_t progress, void *ctx) {
  size_t length = strlen(text);
  if (!length || length > 128 || start > e->length)
    return -1;
  unsigned char *buffer = malloc(BUFFER + 128);
  if (!buffer)
    return -1;
  int result = 1;
  size_t carry = 0;
  uint32_t offset = start;
  while (offset < e->length) {
    size_t n;
    if (pos_editor_read(e, offset, buffer + carry, BUFFER, &n) || !n) {
      result = -1;
      break;
    }
    for (size_t i = 0; i + length <= n + carry; ++i)
      if (!memcmp(buffer + i, text, length)) {
        *found = offset - carry + i;
        result = 0;
        goto done;
      }
    offset += n;
    size_t total = n + carry;
    carry = total < length - 1 ? total : length - 1;
    memmove(buffer, buffer + total - carry, carry);
    if (progress && progress(ctx, "Finding text", offset, e->length)) {
      result = -1;
      break;
    }
  }
done:
  free(buffer);
  return result;
}
int pos_editor_line(pos_editor_t *e, uint32_t line, uint32_t *offset,
                    pos_io_progress_t progress, void *ctx) {
  if (!line)
    return -1;
  *offset = 0;
  if (line == 1)
    return 0;
  unsigned char *buffer = malloc(BUFFER);
  if (!buffer)
    return -1;
  int result = 1;
  uint32_t current = 1, at = 0;
  while (at < e->length) {
    size_t n;
    if (pos_editor_read(e, at, buffer, BUFFER, &n) || !n) {
      result = -1;
      break;
    }
    for (size_t i = 0; i < n; ++i)
      if (buffer[i] == '\n' && ++current == line) {
        *offset = at + i + 1;
        result = 0;
        goto done;
      }
    at += n;
    if (progress && progress(ctx, "Finding line", at, e->length)) {
      result = -1;
      break;
    }
  }
done:
  free(buffer);
  return result;
}
