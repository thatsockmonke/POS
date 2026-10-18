#include "pos_wave.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned u16(const unsigned char *p) {
  return p[0] | (unsigned)p[1] << 8;
}
static uint32_t u32(const unsigned char *p) {
  return u16(p) | (uint32_t)u16(p + 2) << 16;
}
static void put(unsigned char *p, uint32_t v, unsigned n) {
  while (n--) {
    *p++ = (unsigned char)v;
    v >>= 8;
  }
}
static int error(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
typedef struct {
  FILE *file;
  uint32_t offset, frames;
  unsigned channels, rate;
} wave_t;
static int open_wave(const char *root, const char *relative, wave_t *w,
                     pos_error_t *e) {
  char path[POS_FS_PATH];
  struct stat st;
  uint32_t size;
  if (!pos_files_mutable(root, relative) || pos_fs_join(path, root, relative) ||
      pos_fs_lstat(path, &st) || !S_ISREG(st.st_mode) ||
      pos_fs_file_size(path, &size) || size > INT32_MAX)
    return error(e, "Choose a user-owned PCM WAV.");
  w->file = fopen(path, "rb");
  if (!w->file)
    return error(e, "Cannot read WAV.");
  unsigned char h[40];
  bool format = false, data = false;
  if (fread(h, 1, 12, w->file) != 12 || memcmp(h, "RIFF", 4) ||
      memcmp(h + 8, "WAVE", 4) || u32(h + 4) + 8ull > size)
    goto bad;
  uint32_t bound = u32(h + 4) + 8, pos = 12;
  while (pos + 8ull <= bound) {
    if (fseek(w->file, pos, SEEK_SET) || fread(h, 1, 8, w->file) != 8)
      goto bad;
    uint32_t n = u32(h + 4);
    if ((uint64_t)pos + 8 + n + (n & 1) > bound)
      goto bad;
    if (!memcmp(h, "fmt ", 4)) {
      if (format || n < 16 || fread(h, 1, 16, w->file) != 16 || u16(h) != 1 ||
          u16(h + 14) != 16)
        goto bad;
      w->channels = u16(h + 2);
      w->rate = u32(h + 4);
      if ((w->channels != 1 && w->channels != 2) || w->rate < 8000 ||
          w->rate > 48000 || u16(h + 12) != w->channels * 2 ||
          u32(h + 8) != w->rate * w->channels * 2)
        goto bad;
      format = true;
    } else if (!memcmp(h, "data", 4)) {
      if (!format || data || n % (w->channels * 2))
        goto bad;
      w->offset = pos + 8;
      w->frames = n / (w->channels * 2);
      data = true;
    }
    pos += 8 + n + (n & 1);
  }
  if (!format || !data || !w->frames)
    goto bad;
  return 0;
bad:
  fclose(w->file);
  w->file = NULL;
  return error(
      e, "Unsupported/damaged WAV. Use 16-bit PCM mono/stereo, 8-48 kHz.");
}
static int pass(wave_t *w, uint32_t first, uint32_t end, FILE *out,
                unsigned gain_peak, uint16_t *peaks, unsigned *peak,
                pos_io_progress_t progress, void *ctx, pos_error_t *e) {
  unsigned char *b = malloc(2048);
  if (!b)
    return error(e, "Not enough memory for audio editing.");
  int result = 0;
  uint32_t at = first;
  if (fseek(w->file, w->offset + (long)first * w->channels * 2, SEEK_SET))
    result = -1;
  while (!result && at < end) {
    uint32_t frames = end - at;
    if (frames > 1024 / w->channels)
      frames = 1024 / w->channels;
    size_t bytes = frames * w->channels * 2;
    if (fread(b, 1, bytes, w->file) != bytes) {
      result = error(e, "WAV read failed; source preserved.");
      break;
    }
    for (unsigned j = 0; j < frames * w->channels; ++j) {
      int sample = (int16_t)u16(b + j * 2);
      unsigned amplitude = sample < 0 ? -sample : sample;
      if (peak && amplitude > *peak)
        *peak = amplitude;
      if (peaks) {
        unsigned bin = (uint64_t)(at + j / w->channels) * 240 / w->frames;
        if (amplitude > peaks[bin])
          peaks[bin] = amplitude;
      }
      if (out && gain_peak) {
        int adjusted = (int)((int64_t)sample * 31128 / gain_peak);
        if (adjusted > 32767)
          adjusted = 32767;
        if (adjusted < -32768)
          adjusted = -32768;
        put(b + j * 2, (uint16_t)adjusted, 2);
      }
    }
    if (out && fwrite(b, 1, bytes, out) != bytes) {
      result = error(e, "SD write failed; partial edit retained.");
      break;
    }
    at += frames;
    if (progress && ((at - first) % 8192 == 0 || at == end) &&
        progress(ctx, out ? "Saving edited WAV" : "Scanning waveform",
                 at - first, end - first))
      result = error(e, "Edit cancelled; source preserved.");
  }
  free(b);
  return result;
}
int pos_wave_scan(const char *root, const char *relative, pos_wave_info_t *info,
                  pos_io_progress_t p, void *ctx, pos_error_t *e) {
  wave_t w = {0};
  memset(info, 0, sizeof(*info));
  if (open_wave(root, relative, &w, e))
    return -1;
  info->rate = w.rate;
  info->channels = w.channels;
  info->frames = w.frames;
  int result = pass(&w, 0, w.frames, NULL, 0, info->peaks, NULL, p, ctx, e);
  fclose(w.file);
  return result;
}
int pos_wave_edit(const char *root, const char *relative, uint32_t first,
                  uint32_t end, bool normalize, char saved[241],
                  pos_io_progress_t p, void *ctx, pos_error_t *e) {
  wave_t w = {0};
  if (open_wave(root, relative, &w, e))
    return -1;
  int result = -1;
  FILE *out = NULL;
  unsigned peak = 0;
  char *work = malloc(POS_FS_PATH * 2);
  if (!work) {
    error(e, "Not enough memory for audio edit.");
    goto done;
  }
  char *target = work, *partial = work + POS_FS_PATH;
  if (first >= end || end > w.frames) {
    error(e, "Trim must retain at least one frame inside the recording.");
    goto done;
  }
  if (normalize && pass(&w, first, end, NULL, 0, NULL, &peak, p, ctx, e))
    goto done;
  if (pos_fs_join(target, root, "Recordings") || pos_fs_mkdirs(target))
    goto done;
  unsigned n;
  for (n = 1; n <= 9999; ++n) {
    snprintf(saved, 241, "Recordings/Edit-%04u.wav", n);
    if (pos_fs_join(target, root, saved) || strlen(target) + 6 > POS_FS_PATH)
      goto done;
    memcpy(partial, target, strlen(target));
    memcpy(partial + strlen(target), ".part", 6);
    struct stat st;
    if (pos_fs_lstat(target, &st) && pos_fs_lstat(partial, &st))
      break;
  }
  if (n > 9999) {
    error(e, "No free edited-recording names.");
    goto done;
  }
  out = fopen(partial, "wbx");
  if (!out) {
    error(e, "Cannot create edited WAV; source preserved.");
    goto done;
  }
  unsigned char header[44] = {0};
  uint32_t bytes = (end - first) * w.channels * 2;
  memcpy(header, "RIFF", 4);
  put(header + 4, bytes + 36, 4);
  memcpy(header + 8, "WAVEfmt ", 8);
  put(header + 16, 16, 4);
  put(header + 20, 1, 2);
  put(header + 22, w.channels, 2);
  put(header + 24, w.rate, 4);
  put(header + 28, w.rate * w.channels * 2, 4);
  put(header + 32, w.channels * 2, 2);
  put(header + 34, 16, 2);
  memcpy(header + 36, "data", 4);
  put(header + 40, bytes, 4);
  if (fwrite(header, 1, 44, out) != 44 ||
      pass(&w, first, end, out, normalize ? peak : 0, NULL, NULL, p, ctx, e) ||
      fflush(out) || fsync(fileno(out)))
    goto done;
  if (fclose(out)) {
    out = NULL;
    goto done;
  }
  out = NULL;
  /* Independent streamed readback, including the generated header. */
  pos_wave_info_t *check = malloc(sizeof(*check));
  char relpart[241];
  snprintf(relpart, sizeof(relpart), "%s.part", saved);
  if (!check || pos_wave_scan(root, relpart, check, p, ctx, e) ||
      check->frames != end - first || check->rate != w.rate ||
      check->channels != w.channels) {
    free(check);
    goto done;
  }
  free(check);
  /* Verify all written bytes against the source transform, rather than only
   * format. */
  FILE *verify = fopen(partial, "rb");
  unsigned char a[512], b[512];
  bool bad = !verify;
  if (!bad && (fread(a, 1, 44, verify) != 44 || memcmp(a, header, 44)))
    bad = true;
  if (fseek(w.file, w.offset + (long)first * w.channels * 2, SEEK_SET))
    bad = true;
  uint32_t remaining = bytes;
  while (!bad && remaining) {
    size_t take = remaining > 512 ? 512 : remaining;
    if (fread(a, 1, take, w.file) != take ||
        fread(b, 1, take, verify) != take) {
      bad = true;
      break;
    }
    if (normalize && peak)
      for (size_t j = 0; j < take; j += 2) {
        int value = (int)((int64_t)(int16_t)u16(a + j) * 31128 / peak);
        put(a + j, (uint16_t)value, 2);
      }
    if (memcmp(a, b, take))
      bad = true;
    remaining -= (uint32_t)take;
    if (p && ((bytes - remaining) % 65536 == 0 || !remaining) &&
        p(ctx, "Verifying edited WAV", bytes - remaining, bytes))
      bad = true;
  }
  if (verify)
    fclose(verify);
  if (bad || pos_fs_publish_new(partial, target)) {
    error(
        e,
        "WAV verification/publication failed; original and partial retained.");
    goto done;
  }
  result = 0;
done:
  if (out)
    fclose(out);
  fclose(w.file);
  free(work);
  if (result && e && !*e->message)
    error(e, "Edit incomplete; original and partial retained.");
  return result;
}
