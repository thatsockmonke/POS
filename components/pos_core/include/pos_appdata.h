#pragma once
#include "pos_files.h"
#define POS_APPDATA_MAX 1024
typedef struct {
  uint32_t schema, generation;
  char json[POS_APPDATA_MAX + 1];
} pos_appdata_t;
/* One schema step at a time, writing a separate bounded JSON document.
 * Return -1 to preserve an unsupported schema. Large save data belongs in
 * streamed companion files; this envelope is for metadata and references. */
typedef int (*pos_appdata_migrator_t)(void *, uint32_t, const char *, char *,
                                      size_t);
/* 0 current, 1 absent, 2 migrated in memory, -1 corrupt, -2 future/unsupported.
 * Load never edits a save. Save refuses to overwrite either future slot. */
int pos_appdata_load(const char *root, const char *app, uint32_t schema,
                     pos_appdata_migrator_t, void *, pos_appdata_t *,
                     pos_error_t *);
int pos_appdata_save(const char *root, const char *app, pos_appdata_t *,
                     pos_error_t *);
/* Real built-in migration: counter schema 0 {"value":N} -> 1 {"count":N}. */
int pos_counter_load(const char *root, int *count, pos_error_t *);
int pos_counter_save(const char *root, int count, pos_error_t *);
