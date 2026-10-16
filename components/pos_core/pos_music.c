#include "pos_music.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "vendor/minimp3/minimp3.h"
struct pos_music {
  FILE *file;
  bool mp3;
  unsigned rate, channels;
  uint32_t length, offset, remaining, total;
  uint64_t frames;
  bool ended;
  mp3dec_t decoder;
  mp3dec_scratch_t scratch;
  uint8_t input[8192];
  size_t used;
  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
};
static int fail(pos_error_t *e, const char *text) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return -1;
}
static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) {
  return le16(p) | (uint32_t)le16(p + 2) << 16;
}
bool pos_music_supported(const char *name) {
  const char *ext = strrchr(name, '.');
  return ext && (!strcasecmp(ext, ".wav") || !strcasecmp(ext, ".mp3"));
}
pos_music_t *pos_music_open(const char *root, const char *relative,
                            pos_error_t *e) {
  char full[POS_FS_PATH];
  uint32_t size;
  if (!pos_music_supported(relative) || !pos_files_mutable(root, relative) ||
      pos_fs_join(full, root, relative) || pos_fs_file_size(full, &size) ||
      size > INT32_MAX) {
    fail(e, "Choose an MP3 or 16-bit PCM WAV user file below 2 GiB.");
    return NULL;
  }
  pos_music_t *m = calloc(1, sizeof(*m));
  if (!m) {
    fail(e, "Not enough memory for streaming audio.");
    return NULL;
  }
  m->file = fopen(full, "rb");
  if (!m->file) {
    free(m);
    fail(e, "Cannot open audio file.");
    return NULL;
  }
  m->length = size;
  m->mp3 = !strcasecmp(strrchr(relative, '.'), ".mp3");
  if (m->mp3) {
    mp3dec_init(&m->decoder);
    return m;
  }
  uint8_t header[16];
  bool format = false;
  if (fread(header, 1, 12, m->file) != 12 || memcmp(header, "RIFF", 4) ||
      memcmp(header + 8, "WAVE", 4))
    goto invalid;
  uint32_t at = 12;
  while ((uint64_t)at + 8 <= size) {
    if (fread(header, 1, 8, m->file) != 8)
      goto invalid;
    uint32_t length = le32(header + 4);
    at += 8;
    if ((uint64_t)at + length > size)
      goto invalid;
    if (!memcmp(header, "fmt ", 4)) {
      if (length < 16 || fread(header, 1, 16, m->file) != 16 ||
          le16(header) != 1 || le16(header + 14) != 16)
        goto invalid;
      m->channels = le16(header + 2);
      m->rate = le32(header + 4);
      if ((m->channels != 1 && m->channels != 2) || m->rate < 8000 ||
          m->rate > 48000 || le16(header + 12) != m->channels * 2)
        goto invalid;
      format = true;
    } else if (!memcmp(header, "data", 4)) {
      if (!format || length % (m->channels * 2))
        goto invalid;
      m->offset = at;
      m->remaining = m->total = length;
      return m;
    }
    uint64_t next = (uint64_t)at + length + (length & 1);
    if (next > size || next > INT32_MAX || fseek(m->file, next, SEEK_SET))
      goto invalid;
    at = next;
  }
invalid:
  pos_music_close(m);
  fail(e, "Unsupported or damaged WAV. Use PCM signed 16-bit mono/stereo, 8-48 "
          "kHz.");
  return NULL;
}
int pos_music_read(pos_music_t *m, const int16_t **pcm, size_t *frames,
                   unsigned volume, pos_error_t *e) {
  *frames = 0;
  *pcm = m->pcm;
  if (m->ended)
    return 0;
  if (volume > 100)
    volume = 100;
  if (!m->mp3) {
    size_t bytes = m->remaining < 2048 ? m->remaining : 2048;
    if (!bytes) {
      m->ended = true;
      return 0;
    }
    if (fread(m->input, 1, bytes, m->file) != bytes)
      return fail(e, "Audio read failed; check SD.");
    *frames = bytes / (m->channels * 2);
    for (size_t i = 0; i < *frames; ++i) {
      m->pcm[i * 2] = (int16_t)le16(m->input + i * m->channels * 2);
      m->pcm[i * 2 + 1] = (int16_t)le16(m->input + i * m->channels * 2 +
                                        (m->channels == 2 ? 2 : 0));
    }
    m->remaining -= bytes;
  } else {
    for (unsigned attempts = 0; attempts < 32; ++attempts) {
      if (m->used < sizeof(m->input) / 2 && !feof(m->file))
        m->used +=
            fread(m->input + m->used, 1, sizeof(m->input) - m->used, m->file);
      if (ferror(m->file))
        return fail(e, "MP3 read failed; check SD.");
      mp3dec_frame_info_t info = {0};
      int samples = mp3dec_decode_frame_with_scratch(
          &m->decoder, m->input, m->used, m->pcm, &info, &m->scratch);
      size_t consumed = info.frame_bytes > 0 ? (size_t)info.frame_bytes : 0;
      if (!consumed) {
        if (feof(m->file)) {
          if (!m->frames)
            return fail(e, "No decodable MP3 audio frames were found.");
          m->ended = true;
          return 0;
        }
        consumed = m->used > 3 ? m->used - 3 : 1;
      }
      if (consumed > m->used)
        return fail(e, "Invalid MP3 frame.");
      memmove(m->input, m->input + consumed, m->used - consumed);
      m->used -= consumed;
      if (!samples)
        continue;
      if (info.hz < 8000 || info.hz > 48000 ||
          (info.channels != 1 && info.channels != 2))
        return fail(e, "Unsupported MP3 sample format.");
      m->rate = info.hz;
      m->channels = info.channels;
      *frames = samples;
      if (info.channels == 1)
        for (int i = samples - 1; i >= 0; --i)
          m->pcm[i * 2] = m->pcm[i * 2 + 1] = m->pcm[i];
      break;
    }
    if (!*frames)
      return 1; /* Yield UI between bounded searches for the next frame. */
  }
  for (size_t i = 0; i < *frames * 2; ++i)
    m->pcm[i] = (int32_t)m->pcm[i] * (int32_t)volume / 100;
  m->frames += *frames;
  return 0;
}
pos_music_status_t pos_music_status(const pos_music_t *m) {
  pos_music_status_t s = {.rate = m->rate,
                          .channels = m->channels,
                          .frames = m->frames,
                          .ended = m->ended};
  s.total_bytes = m->mp3 ? m->length : m->total;
  long at = ftell(m->file);
  s.bytes =
      m->mp3
          ? (at >= 0 && (uint64_t)at >= m->used ? (uint32_t)(at - m->used) : 0)
          : m->total - m->remaining;
  return s;
}
void pos_music_close(pos_music_t *m) {
  if (m) {
    if (m->file)
      fclose(m->file);
    free(m);
  }
}
int pos_music_track(const char *root, const char *folder, unsigned index,
                    char relative[241], unsigned *total) {
  *total = 0;
  char full[POS_FS_PATH];
  if ((*folder && !pos_fs_archive_path(folder)) || pos_fs_join(full, root, folder))
    return -1;
  DIR *d = opendir(full);
  if (!d)
    return -1;
  struct dirent *entry;
  bool found = false;
  while ((entry = readdir(d)))
    if (pos_music_supported(entry->d_name)) {
      char path[241];
      if (snprintf(path, sizeof(path), "%s%s%s", folder, *folder ? "/" : "", entry->d_name) >=
              (int)sizeof(path) ||
          !pos_files_mutable(root, path))
        continue;
      pos_fs_join(full, root, path);
      struct stat st;
      if (pos_fs_lstat(full, &st) || !S_ISREG(st.st_mode))
        continue;
      if ((*total)++ == index) {
        strcpy(relative, path);
        found = true;
      }
    }
  closedir(d);
  return found ? 0 : 1;
}
int pos_music_playlist_add(const char *root, const char *playlist,
                           const char *track, pos_error_t *e) {
  if (!pos_files_mutable(root, playlist) || !pos_files_mutable(root, track) ||
      !pos_music_supported(track))
    return fail(e, "Choose user playlist and audio paths.");
  char path[POS_FS_PATH];
  if (pos_fs_join(path, root, playlist))
    return -1;
  FILE *f = fopen(path, "ab");
  if (!f)
    return fail(e, "Cannot open playlist.");
  int bad = fprintf(f, "/%s\n", track) < 0 || fflush(f) || fsync(fileno(f));
  if (fclose(f))
    bad = 1;
  return bad ? -1 : 0;
}
int pos_music_playlist_track(const char *root, const char *playlist,
                             unsigned index, char relative[241],
                             unsigned *total) {
  *total = 0;
  if (!pos_files_mutable(root, playlist))
    return -1;
  char path[POS_FS_PATH];
  pos_fs_join(path, root, playlist);
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  char line[244];
  bool found = false;
  while (fgets(line, sizeof(line), f)) {
    if (!strchr(line, '\n') && !feof(f)) {
      int c;
      while ((c = fgetc(f)) != EOF && c != '\n')
        ;
      continue;
    }
    line[strcspn(line, "\r\n")] = 0;
    char *p = line;
    if (!*p || *p == '#')
      continue;
    if (*p == '/')
      ++p;
    if (!pos_files_mutable(root, p) || !pos_music_supported(p))
      continue;
    if ((*total)++ == index) {
      strcpy(relative, p);
      found = true;
    }
  }
  int bad = ferror(f);
  fclose(f);
  return bad ? -1 : found ? 0 : 1;
}
