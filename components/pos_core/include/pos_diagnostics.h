#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pos_fs.h"
typedef struct {uint32_t failures;bool pending,forced;} pos_boot_policy_t;
bool pos_boot_policy_begin(pos_boot_policy_t *,bool supervised,bool crash_reset);
void pos_boot_policy_healthy(pos_boot_policy_t *);
typedef struct {
  char boot_version[32],panic_version[32],panic_build[65];
  uint32_t reset_reason,failed_boots,core,exception,pc,sp,a0,cause,address;
  bool panic_snapshot;
  int64_t utc;
} pos_crash_record_t;
/* New readable JSON per event; sync+readback before success. Never deletes logs. */
int pos_crash_record_write(const char *root,const pos_crash_record_t *,char destination[POS_FS_PATH]);
