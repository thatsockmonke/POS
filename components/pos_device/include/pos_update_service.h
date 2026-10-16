#pragma once
#include "pos_update.h"
int pos_update_source_load(pos_update_source_t *source);
int pos_update_source_save(const pos_update_source_t *source);
int pos_update_fetch(void *, const char *url, const char *root,
                     const char *name, uint32_t limit, const char *sha,
                     pos_error_t *);
int pos_update_service_confirm(const char *root, pos_error_t *);
