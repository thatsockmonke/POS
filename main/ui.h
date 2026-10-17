#pragma once
#include "pos_core.h"
#include "pos_install.h"
#include "pos_settings.h"
bool pos_ui_take_home(void);
void pos_ui_set_theme(const pos_background_t *background);
void pos_ui_style(pos_view_t *view);
/* Bounded spring frames; missed frames are skipped on slow displays. */
void pos_ui_spring(pos_view_t *view, bool opening, bool returning);
void pos_ui_message(const char *title, const char *body);
void pos_ui_notice(const char *title, const char *body);
int pos_ui_menu(const char *title, const char *const *items, int count);
int pos_ui_menu_from(const char *title, const char *const *items, int count,
                     int selected);
bool pos_ui_color(const char *title, uint32_t *rgb);
bool pos_ui_background(const char *root, pos_settings_t *settings);
int pos_ui_menu_page(const char *title, const char *const *items, int count,
                     unsigned page, unsigned pages, const char *status);
bool pos_ui_setup(const char *root, pos_settings_t *settings, bool first);
void pos_ui_browse(const char *root, const char *initial);
bool pos_ui_pick_file(const char *root, const char *title, char relative[241]);
bool pos_ui_pick_file_from(const char *root, const char *title,
                           const char *initial, char relative[241]);
void pos_ui_editor(const char *root);
void pos_ui_wifi(const char *root);
void pos_ui_files(const char *root);
void pos_ui_editor_open(const char *root, const char *relative);
void pos_ui_music(const char *root);
void pos_ui_music_file(const char *root, const char *relative);
void pos_ui_share(const char *root, const char *relative);
void pos_ui_sharing(const char *root);
void pos_ui_ble(const char *root, const char *relative, bool receive);
void pos_ui_wifi_export(const char *root, const char *relative);
void pos_ui_expansion(const char *root);
void pos_ui_location(const char *root);
void pos_ui_radio(const char *root);
void pos_ui_range(const char *root);
const char *pos_ui_phase_title(const char *phase);
void pos_ui_install_complete(void);
/* Returns false on global Home/Escape; edits remain local until caller saves.
 */
bool pos_ui_text(const char *title, const char *label, char *text,
                 size_t capacity, bool secret);
int pos_ui_progress(void *ctx, const char *phase, const char *name,
                    uint64_t done, uint64_t total);
void pos_desktop(const char *root, bool recovery_only);

bool pos_ui_timezone(char timezone[64]);
void pos_ui_settings(const char *root, pos_settings_t *settings);
void pos_ui_updates(const char *root);

void pos_ui_recorder(const char *root, pos_settings_t *settings);
void pos_ui_files_from(const char *root,const char *folder);
void pos_ui_instruments(void);

void pos_ui_deploy(const char *root);

void pos_ui_maps(const char *root);
