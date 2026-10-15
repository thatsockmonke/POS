#pragma once
#include "pos_install.h"
#include <stdbool.h>
#include <stdint.h>
#define POS_BACKUP_SET "POS-Filesystem-Backup"
#define POS_BACKUP_NAME "POS-Filesystem-Backup.zip"
#define POS_BACKUP_FILES 2048
/* Current ESP/newlib FAT seek interfaces use signed 32-bit offsets. */
#define POS_BACKUP_LIMIT 0x7fffffffu
typedef struct {
  uint64_t bytes, required;
  uint32_t files, conflicts, directories;
  bool directory_backup;
  char source_version[32];
} pos_backup_info_t;
/* Nonzero progress return requests cancellation; never marks partial output
 * complete. */
typedef int (*pos_recovery_progress_t)(void *ctx, const char *phase,
                                       const char *file, uint64_t done,
                                       uint64_t total);
void pos_recovery_source_version(const char *root, char version[32]);
int pos_backup_scan(const char *root, pos_backup_info_t *info,
                    pos_recovery_progress_t progress, void *ctx,
                    pos_error_t *error);
int pos_backup_create(const char *root, const char *version, uint64_t available,
                      int64_t creation_time, pos_recovery_progress_t progress,
                      void *ctx, pos_error_t *error);
int pos_backup_verify(const char *archive, pos_backup_info_t *info,
                      pos_recovery_progress_t progress, void *ctx,
                      pos_error_t *error);
int pos_backup_matches(const char *root, const char *archive,
                       pos_error_t *error);
int pos_backup_restore(const char *root, const char *archive,
                       uint64_t available, pos_backup_info_t *info,
                       pos_recovery_progress_t progress, void *ctx,
                       pos_error_t *error);
/* Verify backup + package before retiring the existing tree. Never removes
 * POS.previous. */
int pos_reinstall_prepare(const char *root, const pos_package_t *package,
                          const pos_platform_t *platform, pos_error_t *error);
/* Repair only the exact same package-owned System files; user data is
 * untouched. */
int pos_repair_system(const char *root, const pos_package_t *package,
                      const pos_platform_t *platform, pos_error_t *error);

/* Automatically selects a present ZIP or completed directory backup; refuses
 * ambiguity. */
int pos_backup_find(const char *root, char path[512], pos_error_t *error);
int pos_backup_identity(const char *archive, char hash[65], pos_error_t *error);
/* Directory backup is also available explicitly for small collections. */
int pos_backup_set_scan(const char *root, pos_backup_info_t *info,
                        pos_recovery_progress_t progress, void *ctx,
                        pos_error_t *error);
int pos_backup_set_create(const char *root, const char *version,
                          uint64_t available, int64_t creation_time,
                          pos_recovery_progress_t progress, void *ctx,
                          pos_error_t *error);
int pos_backup_set_verify(const char *archive, pos_backup_info_t *info,
                          pos_recovery_progress_t progress, void *ctx,
                          pos_error_t *error);
int pos_backup_set_restore(const char *root, const char *archive,
                           uint64_t available, pos_backup_info_t *info,
                           pos_recovery_progress_t progress, void *ctx,
                           pos_error_t *error);
int pos_backup_set_matches(const char *root, const char *archive,
                           pos_error_t *error);

int pos_backup_set_resume(const char *root, const char *version,
                          uint64_t available, int64_t creation_time,
                          pos_recovery_progress_t progress, void *ctx,
                          pos_error_t *error);

int pos_backup_matches_progress(const char *root, const char *archive,
                                pos_recovery_progress_t progress, void *ctx,
                                pos_error_t *error);
int pos_backup_set_matches_progress(const char *root, const char *archive,
                                    pos_recovery_progress_t progress, void *ctx,
                                    pos_error_t *error);

int pos_backup_identity_progress(const char *archive, char hash[65],
                                 pos_recovery_progress_t progress, void *ctx,
                                 pos_error_t *error);
