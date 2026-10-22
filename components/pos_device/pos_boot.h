#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>

/* Internal startup presentation; consume skip input without latching Home. */
void pos_boot_animation(QueueHandle_t input, const bool *display_ok);
