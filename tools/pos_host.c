/* Native companion for exercising the exact portable POS services on Linux. */
#include "pos_core.h"
#include "pos_fs.h"
#include "pos_recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

static int progress(void *ctx, const char *phase, const char *name,
                    uint64_t done, uint64_t total) {
  (void)ctx;
  static uint64_t last;
  if (done - last >= 1048576 || !done) {
    fprintf(stderr, "%s: %s %llu / %llu\n", phase, name,
            (unsigned long long)done, (unsigned long long)total);
    last = done;
  }
  return 0;
}
static int preview(const char *root, const char *output, bool boot,
                   unsigned frame) {
  pos_view_t v;
  if (boot) {
    uint32_t hold;
    if (!pos_boot_frame(frame, &v, &hold))
      return 2;
  } else {
    pos_view_clear(&v, "PANPUTER");
    pos_view_line(&v, 2, "Hello, Stevie");
    v.tiles = true;
    v.selected_tile = 0;
    char path[POS_FS_PATH];
    pos_fs_join(path, root, "System/Assets/apps.icons");
    FILE *icons = fopen(path, "rb");
    if (icons) {
      if (fread(v.icons, 1, sizeof(v.icons), icons) != sizeof(v.icons))
        memset(v.icons, 0, sizeof(v.icons));
      fclose(icons);
    }
    pos_view_line(&v, 12, "Counter   Settings  Diagnostic");
    pos_view_line(&v, 14, "Page 1/2   Counter");
    pos_view_line(&v, 15, "WASD: move  Enter: open");
  }
  FILE *f = fopen(output, "wbx");
  if (!f)
    return 1;
  fprintf(f, "P6\n240 135\n255\n");
  int result = 0;
  for (int y = 0; y < 135; ++y)
    for (int x = 0; x < 240; ++x) {
      uint16_t c = pos_view_pixel(&v, x, y);
      unsigned char rgb[] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                             (unsigned char)(((c >> 5) & 63) * 255 / 63),
                             (unsigned char)((c & 31) * 255 / 31)};
      if (fwrite(rgb, 1, 3, f) != 3)
        result = 1;
    }
  if (fclose(f))
    result = 1;
  return result;
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  const char *root = argv[1], *command = argv[2];
  if (!strcmp(command, "boot-frames")) {
    if (argc != 4)
      return 2;
    pos_view_t view;
    uint32_t hold;
    for (unsigned frame = 0; pos_boot_frame(frame, &view, &hold); ++frame) {
      char output[POS_FS_PATH];
      int size = snprintf(output, sizeof(output), "%s/%u.ppm", argv[3], frame);
      if (size < 0 || (size_t)size >= sizeof(output))
        return 2;
      int result = preview(root, output, true, frame);
      if (result)
        return result;
      printf("%u\n", (unsigned)hold);
    }
    return 0;
  }
  if (!strcmp(command, "preview") || !strcmp(command, "boot-preview"))
    return argc == 4 ? preview(root, argv[3], !strcmp(command, "boot-preview"),
                               POS_BOOT_HOLD_FRAME)
                     : 2;
  struct statvfs fs;
  if (statvfs(root, &fs))
    return 1;
  uint64_t free = (uint64_t)fs.f_bavail * fs.f_frsize;
  char archive[POS_FS_PATH];
  pos_fs_join(archive, root, POS_BACKUP_NAME);
  if (argc == 4)
    snprintf(archive, sizeof(archive), "%s", argv[3]);
  else if (strcmp(command, "backup") && strcmp(command, "backup-set") &&
           strcmp(command, "resume-backup") && strcmp(command, "scan")) {
    pos_error_t find_error = {0};
    if (pos_backup_find(root, archive, &find_error)) {
      fprintf(stderr, "%s\n", find_error.message);
      return 1;
    }
  }
  pos_backup_info_t info = {0};
  pos_error_t error = {0};
  int result = -1;
  if (!strcmp(command, "scan"))
    result = pos_backup_scan(root, &info, progress, NULL, &error);
  if (!strcmp(command, "backup"))
    result = pos_backup_create(root, "host", free, time(NULL), progress, NULL,
                               &error);
  if (!strcmp(command, "resume-backup"))
    result = pos_backup_set_resume(root, "host", free, time(NULL), progress,
                                   NULL, &error);
  if (!strcmp(command, "backup-set"))
    result = pos_backup_set_create(root, "host", free, time(NULL), progress,
                                   NULL, &error);
  if (!strcmp(command, "verify"))
    result = pos_backup_verify(archive, &info, progress, NULL, &error);
  if (!strcmp(command, "restore"))
    result =
        pos_backup_restore(root, archive, free, &info, progress, NULL, &error);
  if (result) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  if (strcmp(command, "backup"))
    printf("files=%u bytes=%llu required=%llu conflicts=%u\n",
           (unsigned)info.files, (unsigned long long)info.bytes,
           (unsigned long long)info.required, (unsigned)info.conflicts);
  return 0;
}
