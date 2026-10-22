#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
/* Match the actual POS scheduler rate, including sub-tick deadlines. */
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10))
