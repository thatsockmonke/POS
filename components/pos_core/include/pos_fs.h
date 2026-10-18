#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#define POS_FS_PATH 512
typedef int (*pos_io_progress_t)(void *, const char *, uint64_t, uint64_t);
int pos_fs_join(char out[POS_FS_PATH], const char *root, const char *relative);
int pos_fs_mkdirs(const char *path);
int pos_fs_write_new(const char *path, const void *bytes, size_t length);
int pos_fs_publish_new(const char *temporary, const char *destination);
int pos_fs_layout(const char *root);
/* User-owned SD data: everything except the documented resident
 * system/staging/cache/media. */
bool pos_fs_user_path(const char *relative);
bool pos_fs_archive_path(const char *relative);
int pos_fs_crc_file(const char *path, uint32_t size, uint32_t *crc);
uint32_t pos_crc32(uint32_t previous, const void *data, size_t length);
bool pos_fs_json_safe(const char *text, size_t length);
int pos_fs_lstat(const char *path, struct stat *st);

/* FAT32 permits files through UINT32_MAX; ESP stat uses a signed 32-bit field.
 */
int pos_fs_file_size(const char *path, uint32_t *size);

int pos_fs_crc_file_progress(const char *path, uint32_t size, uint32_t *crc,
                             int (*progress)(void *, uint64_t, uint64_t),
                             void *ctx);

/* Durable line journal; isolate a truncated final record after power loss. */
int pos_fs_append_line(const char *path,const char *line);
