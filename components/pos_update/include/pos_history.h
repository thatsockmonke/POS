#pragma once
#include "pos_update.h"
#define POS_HISTORY_ROOT "POS/.History"
#define POS_HISTORY_LIMIT 15
typedef struct { char id[65], release[32], runtime[32]; uint32_t sequence; } pos_history_entry_t;
typedef int (*pos_history_read_t)(void *,uint32_t,void *,size_t);
/* Completed entries have trusted signed metadata and matching firmware/resources. */
int pos_history_list(const char *,const char *,pos_history_entry_t *,unsigned,unsigned *,pos_error_t *);
int pos_history_capture(const char *,const char *signed_manifest,const char *key,
                        pos_history_read_t,void *,const pos_platform_t *,const char *preserve_id,pos_error_t *);
int pos_history_rollback(const char *,const char *id,const char *key,
                         const pos_update_platform_t *,pos_error_t *);

int pos_history_capture_from(const char *root,const char *system,const char *signed_manifest,const char *key,
  pos_history_read_t,void *,const pos_platform_t *,const char *preserve_id,pos_error_t *);
