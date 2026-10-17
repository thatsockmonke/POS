#include "pos_recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct pos_recorder {
  FILE *file;
  uint8_t io_buffer[4096];
  char relative[241], path[POS_FS_PATH], part[POS_FS_PATH];
  uint32_t frames, crc;
  bool failed, complete;
};
static int fail(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
static void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(v >> (8 * i));
}
static void header(uint8_t h[44], uint32_t bytes) {
  memset(h, 0, 44);
  memcpy(h, "RIFF", 4);
  put32(h + 4, bytes + 36);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16);
  h[20] = 1;
  h[22] = 1;
  put32(h + 24, POS_RECORD_RATE);
  put32(h + 28, POS_RECORD_RATE * 2);
  h[32] = 2;
  h[34] = 16;
  memcpy(h + 36, "data", 4);
  put32(h + 40, bytes);
}
pos_recorder_t *pos_recorder_open(const char *root, pos_error_t *e) {
  char folder[POS_FS_PATH];
  if (!pos_files_mutable(root, POS_RECORDINGS) ||
      pos_fs_join(folder, root, POS_RECORDINGS) || pos_fs_mkdirs(folder)) {
    fail(e, "Recordings must be an ordinary writable user folder.");
    return NULL;
  }
  pos_recorder_t *r = calloc(1, sizeof(*r));
  if (!r) {
    fail(e, "Not enough memory to record.");
    return NULL;
  }
  struct stat st;
  bool found = false;
  for (unsigned i = 1; i <= 99999; ++i) {
    snprintf(r->relative, sizeof(r->relative),
             POS_RECORDINGS "/Recording-%05u.wav", i);
    if (pos_fs_join(r->path, root, r->relative) ||
        snprintf(r->part, sizeof(r->part), "%s.part", r->path) >=
            (int)sizeof(r->part))
      break;
    if (pos_files_mutable(root, r->relative) && pos_fs_lstat(r->path, &st) &&
        pos_fs_lstat(r->part, &st)) {
      found = true;
      break;
    }
  }
  if (!found || !(r->file = fopen(r->part, "wbx"))) {
    fail(e, "Cannot create a recording. Check SD space and Recordings folder.");
    free(r);
    return NULL;
  }
  if (setvbuf(r->file, (char *)r->io_buffer, _IOFBF, sizeof(r->io_buffer))) {
    fail(e, "Cannot allocate recording stream buffer.");
    pos_recorder_close(r);
    return NULL;
  }
  uint8_t h[44];
  header(h, 0);
  if (fwrite(h, 1, 44, r->file) != 44 || fflush(r->file) ||
      fsync(fileno(r->file))) {
    fail(e, "Cannot write WAV header. Partial file retained.");
    pos_recorder_close(r);
    return NULL;
  }
  return r;
}
int pos_recorder_write(pos_recorder_t *r, const int16_t *samples, size_t n,
                       pos_error_t *e) {
  if (!r || !r->file || r->failed || r->complete || n > 1024 ||
      (n && !samples) || n > ((INT32_MAX - 44u) / 2u - r->frames))
    return fail(
        e, "Recording unavailable or 2 GiB WAV limit reached; stop and save.");
  uint8_t bytes[2048];
  for (size_t i = 0; i < n; ++i) {
    bytes[i * 2] = (uint16_t)samples[i];
    bytes[i * 2 + 1] = (uint16_t)samples[i] >> 8;
  }
  if (fwrite(bytes, 2, n, r->file) != n) {
    r->failed = true;
    return fail(e, "SD write failed. Recover the .wav.part file in Recorder.");
  }
  r->crc = pos_crc32(r->crc, bytes, n * 2);
  uint32_t before = r->frames;
  r->frames += (uint32_t)n;
  if (before / (POS_RECORD_RATE * 2) != r->frames / (POS_RECORD_RATE * 2) &&
      (fflush(r->file) || fsync(fileno(r->file)))) {
    r->failed = true;
    return fail(e, "SD sync failed. Partial recording retained.");
  }
  return 0;
}
uint32_t pos_recorder_frames(const pos_recorder_t *r) { return r->frames; }
const char *pos_recorder_path(const pos_recorder_t *r) { return r->relative; }
static int finalize(FILE *f, const char *part, const char *path, uint32_t bytes,
                    uint32_t crc, bool check, pos_error_t *e) {
  uint8_t h[44], actual[44], buffer[2048];
  header(h, bytes);
  bool bad = fseek(f, 0, SEEK_SET) || fwrite(h, 1, 44, f) != 44 || fflush(f) ||
             fsync(fileno(f));
  if (fclose(f))
    bad = true;
  if (bad)
    return fail(e, "WAV finalization failed; partial retained.");
  f = fopen(part, "rb");
  if (!f)
    return fail(e, "Cannot verify partial recording.");
  uint32_t remaining = bytes, got = 0;
  bad = fread(actual, 1, 44, f) != 44 || memcmp(h, actual, 44);
  while (!bad && remaining) {
    size_t n = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (fread(buffer, 1, n, f) != n) {
      bad = true;
      break;
    }
    got = pos_crc32(got, buffer, n);
    remaining -= (uint32_t)n;
  }
  if (check && got != crc)
    bad = true;
  if (ferror(f) || fgetc(f) != EOF)
    bad = true;
  if (fclose(f))
    bad = true;
  if (bad || pos_fs_publish_new(part, path))
    return fail(e, "WAV verification/publication failed; partial retained.");
  return 0;
}
int pos_recorder_finish(pos_recorder_t *r, pos_error_t *e) {
  if (!r || !r->file || r->failed || r->complete)
    return fail(e, "Partial recording retained; use Recover interrupted WAV.");
  FILE *f = r->file;
  r->file = NULL;
  int result = finalize(f, r->part, r->path, r->frames * 2, r->crc, true, e);
  r->complete = !result;
  return result;
}
void pos_recorder_close(pos_recorder_t *r) {
  if (!r)
    return;
  if (r->file)
    fclose(r->file);
  free(r);
}
int pos_recorder_recover(const char *root, const char *relative,
                         pos_error_t *e) {
  char part[POS_FS_PATH], path[POS_FS_PATH];
  struct stat st;
  uint32_t size;
  size_t n = strlen(relative);
  if (strncmp(relative, POS_RECORDINGS "/", 11) || n < 9 ||
      strcmp(relative + n - 9, ".wav.part") ||
      !pos_files_mutable(root, relative) || pos_fs_join(part, root, relative) ||
      pos_fs_file_size(part, &size) || size < 44 || size > INT32_MAX ||
      (size - 44) % 2)
    return fail(e,
                "Select a complete-frame Recordings/*.wav.part below 2 GiB.");
  strcpy(path, part);
  path[strlen(path) - 5] = 0;
  if (!pos_fs_lstat(path, &st))
    return fail(e, "Completed WAV already exists; keep both files.");
  FILE *f = fopen(part, "r+b");
  uint8_t h[44], expected[44];
  header(expected, 0);
  if (!f)
    return fail(e, "Cannot open interrupted recording.");
  /* Only accept our PCM layout; the two interrupted size fields may vary. */
  if (fread(h, 1, 44, f) != 44 || memcmp(h, expected, 4) ||
      memcmp(h + 8, expected + 8, 32)) {
    fclose(f);
    return fail(e, "Not a POS 16 kHz mono recording; original retained.");
  }
  return finalize(f, part, path, size - 44, 0, false, e);
}
