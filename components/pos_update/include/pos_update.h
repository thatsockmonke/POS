#pragma once
#include "pos_install.h"
#include "pos_fs.h"
#include <stdbool.h>
#define POS_UPDATE_MAX 32768
#define POS_UPDATE_JOURNAL "pos-update.pos"
typedef struct {
  char release[32], base_url[512], id[65];
  uint32_t sequence;
  bool firmware;
  pos_package_t *package;
} pos_update_t;
typedef struct {
  void *ctx;
  int (*fetch)(void *ctx,const char *url,const char *directory,const char *name,uint32_t limit,const char *sha,pos_error_t *error);
  pos_platform_t platform;
} pos_update_platform_t;
typedef struct {
  uint32_t format, sequence;
  char url[512], key[513];
} pos_update_source_t;
extern const char pos_update_official_url[], pos_update_official_key[];
/* Defaults preserve custom/partial sources and accepted sequences. */
void pos_update_source_official(pos_update_source_t *source);
bool pos_update_source_use_default(pos_update_source_t *source);
/* Fingerprint is SHA-256 of DER SubjectPublicKeyInfo; only P-256 keys. */
int pos_update_key_fingerprint(const char *pem,char fingerprint[65]);
int pos_update_load(const char *file,const char *trusted_key,const char *current_version,uint32_t minimum_sequence,pos_update_t **out,pos_error_t *error);
void pos_update_free(pos_update_t *update);
int pos_version_compare(const char *left,const char *right,int *comparison);
int pos_update_apply(const char *root,const pos_update_t *,const pos_update_platform_t *,pos_error_t *);
/* Run before validating System, including after OTA rollback. 1=new boot pending. */
int pos_update_recover(const char *root,const char *running_version,pos_error_t *);
/* Only after System validation + OTA confirmation. Keeps previous System on SD. */
int pos_update_confirm(const char *root,uint32_t *sequence,pos_error_t *);

int pos_update_pending_sequence(const char *root,uint32_t *sequence,pos_error_t *);
