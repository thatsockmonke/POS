#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
  int filtered_mv, percent;
  uint8_t bars;
  bool available, full;
  uint64_t high_since_ms, last_ms;
} pos_battery_t;
/* Voltage-based estimate, not a fuel gauge or charger termination signal. */
void pos_battery_sample(pos_battery_t *state, int mv, uint64_t now_ms);
