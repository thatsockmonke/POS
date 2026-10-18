#include "pos_image.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef ESP_PLATFORM
#include "esp32s3/rom/tjpgd.h"
#endif
struct pos_image {
  FILE *file;
  unsigned width, height, bpp, stride, offset;
  bool top, jpeg;
  double cx, cy, scale;
  uint16_t *pixels;
  pos_io_progress_t progress;
  void *ctx;
};
static unsigned le16(const unsigned char *p) {
  return p[0] | (unsigned)p[1] << 8;
}
static uint32_t le32(const unsigned char *p) {
  return le16(p) | (uint32_t)le16(p + 2) << 16;
}
static int error(pos_error_t *e, const char *s) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", s);
  return -1;
}
bool pos_image_supported(const char *p) {
  const char *s = strrchr(p, '.');
  return s && (!strcasecmp(s, ".bmp") || !strcasecmp(s, ".jpg") ||
               !strcasecmp(s, ".jpeg"));
}
#ifdef ESP_PLATFORM
static UINT input(JDEC *d, BYTE *buf, UINT n) {
  pos_image_t *i = d->device;
  return buf ? fread(buf, 1, n, i->file) : fseek(i->file, n, SEEK_CUR) ? 0 : n;
}
static UINT output(JDEC *d, void *data, JRECT *r) {
  pos_image_t *i = d->device;
  const unsigned char *rgb = data;
  for (int y = 0; y < 88; ++y) {
    int sy = (int)floor(i->cy + (y - 43.5) / i->scale);
    if (sy < r->top || sy > r->bottom)
      continue;
    for (int x = 0; x < 240; ++x) {
      int sx = (int)floor(i->cx + (x - 119.5) / i->scale);
      if (sx < r->left || sx > r->right)
        continue;
      const unsigned char *p =
          rgb + ((sy - r->top) * (r->right - r->left + 1) + sx - r->left) * 3;
      i->pixels[y * 240 + x] =
          ((p[0] & 248) << 8) | ((p[1] & 252) << 3) | (p[2] >> 3);
    }
  }
  return !(i->progress &&
           i->progress(i->ctx, "Decoding JPEG", r->bottom, i->height));
}
#endif
pos_image_t *pos_image_open(const char *root, const char *relative,
                            pos_error_t *e) {
  char path[POS_FS_PATH];
  if (!pos_image_supported(relative) || !pos_fs_archive_path(relative) ||
      !pos_fs_user_path(relative) || pos_fs_join(path, root, relative))
    return error(e, "Choose a user JPEG or BMP image."), NULL;
  struct stat st;
  if (pos_fs_lstat(path, &st) || !S_ISREG(st.st_mode))
    return error(e, "Image unavailable."), NULL;
  pos_image_t *i = calloc(1, sizeof(*i));
  if (!i)
    return NULL;
  i->file = fopen(path, "rb");
  unsigned char h[54];
  if (!i->file || fread(h, 1, 54, i->file) != 54)
    goto bad;
  if (h[0] == 'B' && h[1] == 'M') {
    uint32_t height = le32(h + 22);
    i->top = (int32_t)height < 0;
    i->width = le32(h + 18);
    i->height = i->top ? 0u - height : height;
    i->bpp = le16(h + 28) / 8;
    i->offset = le32(h + 10);
    if (le32(h + 14) < 40 || le16(h + 26) != 1 || le32(h + 30) ||
        (le16(h + 28) != 24 && le16(h + 28) != 32) || i->offset < 54)
      goto bad;
    i->stride = (i->width * i->bpp + 3) & ~3u;
    uint32_t size;
    if (pos_fs_file_size(path, &size) ||
        (uint64_t)i->offset + (uint64_t)i->stride * i->height > size)
      goto bad;
  } else if (h[0] == 255 && h[1] == 216) {
#ifdef ESP_PLATFORM
    void *pool = malloc(4096);
    JDEC d;
    rewind(i->file);
    int result = pool ? jd_prepare(&d, input, pool, 4096, i) : JDR_MEM1;
    if (!result) {
      i->width = d.width;
      i->height = d.height;
      i->jpeg = true;
    }
    free(pool);
    if (result)
      goto bad;
#else
    error(e, "JPEG ROM decoding requires the ESP32-S3.");
    pos_image_close(i);
    return NULL;
#endif
  } else
    goto bad;
  if (!i->width || !i->height || i->width > 8192 || i->height > 8192)
    goto bad;
  return i;
bad:
  error(e, "Unsupported or damaged image. Use baseline JPEG or 24/32-bit "
           "uncompressed BMP (max 8192x8192).");
  pos_image_close(i);
  return NULL;
}
void pos_image_size(const pos_image_t *i, unsigned *w, unsigned *h) {
  *w = i->width;
  *h = i->height;
}
int pos_image_render(pos_image_t *i, double cx, double cy, double scale,
                     uint16_t pixels[240 * 88], pos_io_progress_t progress,
                     void *ctx, pos_error_t *e) {
  if (!i || !pixels || !isfinite(cx) || !isfinite(cy) || !isfinite(scale) ||
      scale < 0.001 || scale > 32)
    return error(e, "Invalid image view.");
  memset(pixels, 0, 240 * 88 * 2);
  i->cx = cx;
  i->cy = cy;
  i->scale = scale;
  i->pixels = pixels;
  i->progress = progress;
  i->ctx = ctx;
  if (i->jpeg) {
#ifdef ESP_PLATFORM
    JDEC d;
    void *pool = malloc(4096);
    rewind(i->file);
    int r = pool ? jd_prepare(&d, input, pool, 4096, i) : JDR_MEM1;
    if (!r)
      r = jd_decomp(&d, output, 0);
    free(pool);
    return r ? error(e, "JPEG decode stopped or image is damaged.") : 0;
#else
    return -1;
#endif
  }
  unsigned char *row = malloc(1024);
  if (!row)
    return error(e, "Not enough memory for image block.");
  int last = -1, result = 0;
  for (int y = 0; y < 88; ++y) {
    int sy = (int)floor(cy + (y - 43.5) / scale);
    if (sy < 0 || (unsigned)sy >= i->height)
      continue;
    if (sy == last && y)
      memcpy(pixels + y * 240, pixels + (y - 1) * 240, 480);
    else {
      unsigned r = i->top ? (unsigned)sy : i->height - 1 - (unsigned)sy;
      if (fseek(i->file, i->offset + (long)r * i->stride, SEEK_SET)) {
        result = error(e, "Image read failed.");
        break;
      }
      for (unsigned first = 0; first < i->width; first += 256) {
        unsigned count = i->width - first;
        if (count > 256)
          count = 256;
        size_t bytes = count * i->bpp;
        if (fread(row, 1, bytes, i->file) != bytes) {
          result = error(e, "Image read failed.");
          break;
        }
        for (int x = 0; x < 240; ++x) {
          int sx = (int)floor(cx + (x - 119.5) / scale);
          if (sx < (int)first || sx >= (int)(first + count))
            continue;
          const unsigned char *p = row + (sx - first) * i->bpp;
          pixels[y * 240 + x] =
              ((p[2] & 248) << 8) | ((p[1] & 252) << 3) | (p[0] >> 3);
        }
      }
      if (result)
        break;
      last = sy;
    }
    if (progress && progress(ctx, "Reading image", y, 88)) {
      result = error(e, "Image view cancelled.");
      break;
    }
  }
  free(row);
  return result;
}
void pos_image_close(pos_image_t *i) {
  if (i) {
    if (i->file)
      fclose(i->file);
    free(i);
  }
}
