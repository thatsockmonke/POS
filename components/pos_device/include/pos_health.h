#pragma once
#include <stdbool.h>
void pos_health_begin(bool supervise);
void pos_health_ready(void);
void pos_health_tick(void);
bool pos_health_safe(void);
int pos_health_request_safe(bool enabled);
int pos_health_export(const char *root);
/* Call after the user explicitly chooses normal boot or a planned restart. */
void pos_health_clear(void);

void pos_health_set_confirm_hook(int (*hook)(void));

int pos_health_resume(void);

bool pos_health_healthy(void);
