#include "pos_share.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int fail(pos_error_t *e, const char *text) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return -1;
}
static bool valid(const pos_share_offer_t *o) {
  return o && memchr(o->name, 0, sizeof(o->name)) &&
         pos_fs_archive_path(o->name) && !strchr(o->name, '/') &&
         o->name[0] != '.' && memchr(o->sha256, 0, sizeof(o->sha256)) &&
         strlen(o->sha256) == 64 && strspn(o->sha256, "0123456789abcdef") == 64;
}
int pos_share_assemble(pos_share_assembly_t *a, const void *data, size_t length,
                       pos_share_offer_t *offer) {
  const uint8_t *p = data;
  if (length < 3 || length > 20 || p[0] != 'O')
    return -1;
  if (!p[1]) {
    a->used = 0;
    a->sequence = 0;
  }
  if (p[1] != a->sequence || a->used + length - 2 > sizeof(a->wire))
    return -1;
  memcpy(a->wire + a->used, p + 2, length - 2);
  a->used += length - 2;
  ++a->sequence;
  if (a->used == sizeof(a->wire))
    return pos_share_decode(a->wire, a->used, offer) ? -1 : 1;
  return 0;
}
int pos_share_encode(const pos_share_offer_t *o,
                     uint8_t wire[POS_SHARE_OFFER_BYTES]) {
  if (!valid(o))
    return -1;
  memset(wire, 0, POS_SHARE_OFFER_BYTES);
  wire[0] = 1;
  for (unsigned i = 0; i < 8; ++i)
    wire[1 + i] = o->id >> (8 * i);
  for (unsigned i = 0; i < 4; ++i)
    wire[9 + i] = o->size >> (8 * i);
  memcpy(wire + 13, o->name, strlen(o->name));
  memcpy(wire + 94, o->sha256, 64);
  return 0;
}
int pos_share_decode(const void *data, size_t length, pos_share_offer_t *o) {
  const uint8_t *wire = data;
  if (length != POS_SHARE_OFFER_BYTES || wire[0] != 1 ||
      !memchr(wire + 13, 0, 81))
    return -1;
  memset(o, 0, sizeof(*o));
  for (unsigned i = 0; i < 8; ++i)
    o->id |= (uint64_t)wire[1 + i] << (8 * i);
  for (unsigned i = 0; i < 4; ++i)
    o->size |= (uint32_t)wire[9 + i] << (8 * i);
  memcpy(o->name, wire + 13, 81);
  memcpy(o->sha256, wire + 94, 64);
  return valid(o) ? 0 : -1;
}
typedef struct {
  pos_io_progress_t callback;
  void *ctx;
} offer_progress_t;
static int preparing(void *opaque, uint64_t done, uint64_t total) {
  offer_progress_t *p = opaque;
  return p->callback ? p->callback(p->ctx, "Preparing file offer", done, total)
                     : 0;
}
int pos_share_offer(const char *root, const char *relative,
                    pos_share_offer_t *offer, pos_error_t *e) {
  return pos_share_offer_progress(root, relative, offer, NULL, NULL, e);
}
int pos_share_offer_progress(const char *root, const char *relative,
                             pos_share_offer_t *o, pos_io_progress_t progress,
                             void *ctx, pos_error_t *e) {
  const char *name = strrchr(relative, '/');
  name = name ? name + 1 : relative;
  char full[POS_FS_PATH];
  offer_progress_t preparation = {progress, ctx};
  struct stat st;
  memset(o, 0, sizeof(*o));
  if (strlen(name) > 80 || !pos_files_mutable(root, relative) ||
      pos_fs_join(full, root, relative) || pos_fs_lstat(full, &st) ||
      !S_ISREG(st.st_mode) || pos_fs_file_size(full, &o->size) ||
      pos_hash_file_progress(full, o->size, o->sha256, preparing, &preparation))
    return fail(e, "Choose a readable user file with a filename of at most 80 "
                   "characters.");
  strcpy(o->name, name);
  char prefix[17];
  memcpy(prefix, o->sha256, 16);
  prefix[16] = 0;
  o->id =
      strtoull(prefix, NULL, 16) ^ (uint64_t)pos_crc32(0, name, strlen(name));
  return valid(o) ? 0 : -1;
}
int pos_share_accept(pos_share_receive_t *r, const char *root,
                     const pos_share_offer_t *o, pos_error_t *e) {
  return pos_share_accept_progress(r, root, o, NULL, NULL, e);
}
static int existing_file(void *opaque, uint64_t done, uint64_t total) {
  offer_progress_t *p = opaque;
  return p->callback
             ? p->callback(p->ctx, "Verifying existing file", done, total)
             : 0;
}
int pos_share_accept_progress(pos_share_receive_t *r, const char *root,
                              const pos_share_offer_t *o,
                              pos_io_progress_t progress, void *ctx,
                              pos_error_t *e) {
  memset(r, 0, sizeof(*r));
  if (!valid(o))
    return fail(e, "Invalid file offer.");
  char folder[POS_FS_PATH], relative[160];
  if (pos_fs_join(folder, root, "Downloads") || pos_fs_mkdirs(folder))
    return -1;
  snprintf(relative, sizeof(relative), "Downloads/%s", o->name);
  if (!pos_files_mutable(root, relative) ||
      pos_fs_join(r->destination, root, relative))
    return -1;
  struct stat st;
  if (!pos_fs_lstat(r->destination, &st)) {
    offer_progress_t verification = {progress, ctx};
    uint32_t size;
    char hash[65];
    if (S_ISREG(st.st_mode) && !pos_fs_file_size(r->destination, &size) &&
        size == o->size &&
        !pos_hash_file_progress(r->destination, size, hash, existing_file,
                                &verification) &&
        !strcmp(hash, o->sha256)) {
      r->offer = *o;
      r->offset = size;
      return 1;
    }
    return fail(e, "Destination already exists; it will not be overwritten.");
  }
  if (errno != ENOENT)
    return fail(e, "Destination unavailable; existing data preserved.");
  int n = snprintf(r->part, sizeof(r->part), "%s/.pos-share-%016llx.part",
                   folder, (unsigned long long)o->id);
  if (n < 0 || n >= (int)sizeof(r->part) ||
      snprintf(r->metadata, sizeof(r->metadata), "%s.offer", r->part) >=
          (int)sizeof(r->metadata))
    return -1;
  uint8_t encoded[POS_SHARE_OFFER_BYTES], previous[POS_SHARE_OFFER_BYTES + 1];
  pos_share_encode(o, encoded);
  if (!pos_fs_lstat(r->metadata, &st) && !S_ISREG(st.st_mode))
    return fail(e, "Transfer metadata is not a regular file.");
  FILE *meta = fopen(r->metadata, "rb");
  if (meta) {
    size_t length = fread(previous, 1, sizeof(previous), meta);
    int bad = ferror(meta);
    fclose(meta);
    if (bad || length != sizeof(encoded) ||
        memcmp(previous, encoded, sizeof(encoded)))
      return fail(e, "Different or damaged partial transfer preserved.");
    if (pos_fs_lstat(r->part, &st) || !S_ISREG(st.st_mode) ||
        pos_fs_file_size(r->part, &r->offset) || r->offset > o->size)
      return fail(e,
                  "Partial transfer size is invalid; preserved for recovery.");
    r->file = fopen(r->part, "ab");
  } else {
    if (errno != ENOENT || !pos_fs_lstat(r->part, &st) || errno != ENOENT ||
        pos_fs_write_new(r->metadata, encoded, sizeof(encoded)))
      return fail(e,
                  "Cannot create transfer metadata; existing files retained.");
    r->file = fopen(r->part, "wbx");
  }
  if (!r->file)
    return fail(e, "Cannot open transfer staging file.");
  r->offer = *o;
  return 0;
}
int pos_share_write(pos_share_receive_t *r, uint32_t offset, const void *data,
                    size_t length, pos_error_t *e) {
  if (!r->file || !length || length > 4096 ||
      (uint64_t)offset + length > r->offer.size)
    return fail(e, "Invalid transfer chunk.");
  if (offset < r->offset && (uint64_t)offset + length <= r->offset)
    return 0; /* Lost ACK: acknowledge committed range again. */
  if (offset != r->offset)
    return fail(e, "Chunk offset mismatch; resume from receiver offset.");
  if (fwrite(data, 1, length, r->file) != length || fflush(r->file) ||
      fsync(fileno(r->file)))
    return fail(e, "Transfer write failed. Partial file retained.");
  r->offset += length;
  return 0;
}
typedef struct {
  pos_io_progress_t callback;
  void *ctx;
} verify_t;
static int verifying(void *ctx, uint64_t done, uint64_t total) {
  verify_t *v = ctx;
  return v->callback ? v->callback(v->ctx, "Verifying shared file", done, total)
                     : 0;
}
int pos_share_finish(pos_share_receive_t *r, pos_io_progress_t progress,
                     void *ctx, pos_error_t *e) {
  if (!r->file || r->offset != r->offer.size)
    return fail(e, "Transfer incomplete; partial file retained.");
  int bad = fflush(r->file) || fsync(fileno(r->file));
  if (fclose(r->file))
    bad = 1;
  r->file = NULL;
  char digest[65];
  verify_t v = {progress, ctx};
  if (bad ||
      pos_hash_file_progress(r->part, r->offer.size, digest, verifying, &v) ||
      strcmp(digest, r->offer.sha256))
    return fail(
        e, "Integrity check failed; original and partial files preserved.");
  if (pos_fs_publish_new(r->part, r->destination))
    return fail(e, "Cannot publish verified transfer. Staging file retained.");
  (void)unlink(r->metadata);
  return 0;
}
void pos_share_cancel(pos_share_receive_t *r) {
  if (r->file) {
    fflush(r->file);
    fsync(fileno(r->file));
    fclose(r->file);
    r->file = NULL;
  }
}
