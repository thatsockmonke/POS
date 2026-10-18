#pragma once
#include "pos_core.h"

#define POS_LAUNCHER_APP_COUNT 17
extern const char *const pos_launcher_names[POS_LAUNCHER_APP_COUNT];
int pos_launcher_action(const pos_event_t *event);
/* Stream only the three visible icons. root is the POS directory on SD. */
void pos_launcher_view(const char *root, const char *device,
                       const pos_background_t *background, int selected,
                       pos_view_t *view);
