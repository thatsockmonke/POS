#pragma once
#include "pos_settings.h"
/* 0 ordinary data, 1 valid current settings, 2 migratable legacy, -1 preserve.
 */
int pos_migration_settings(const char *source, const char *relative,
                           pos_settings_t *settings);
int pos_migration_apply(const char *root, const pos_settings_t *legacy);
int pos_migration_record(const char *root, const char *id, const char *source,
                         const char *destination, const char *action);

int pos_migration_decode(const char *relative, const char *text, size_t length,
                         pos_settings_t *settings);
