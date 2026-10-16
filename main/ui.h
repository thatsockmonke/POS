#pragma once
#include "pos_core.h"
#include "pos_install.h"
#include "pos_settings.h"
bool pos_ui_take_home(void);
void pos_ui_message(const char *title, const char *body);
void pos_ui_notice(const char *title, const char *body);
int pos_ui_menu(const char *title, const char *const *items, int count);
int pos_ui_menu_page(const char *title, const char *const *items, int count,
                     unsigned page, unsigned pages, const char *status);
bool pos_ui_setup(const char *root, pos_settings_t *settings, bool first);
void pos_ui_browse(const char *root, const char *initial);
const char *pos_ui_phase_title(const char *phase);
void pos_ui_install_complete(void);
/* Returns false on global Home/Escape; edits remain local until caller saves.
 */
bool pos_ui_text(const char *title, const char *label, char *text,
                 size_t capacity, bool secret);
int pos_ui_progress(void *ctx, const char *phase, const char *name,
                    uint64_t done, uint64_t total);
void pos_desktop(const char *root, bool recovery_only);
