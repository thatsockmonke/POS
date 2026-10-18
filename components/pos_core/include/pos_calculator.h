#pragma once
#include <stdbool.h>
#include <stdint.h>
#define POS_CALC_TEXT 192
#define POS_CALC_DEGREE 8
/* Numeric values use finite IEEE double precision; rational output is bounded.
 */
typedef struct {
  double variables[26], ans;
  uint32_t assigned;
  bool degrees;
} pos_calc_t;
typedef enum {
  POS_CALC_EVALUATE,
  POS_CALC_SOLVE,
  POS_CALC_EXPAND,
  POS_CALC_DIFFERENTIATE,
  POS_CALC_INTEGRATE
} pos_calc_action_t;
/* x is the polynomial indeterminate for CAS operations, even if assigned.
 * Eight parser nesting levels bound the device task stack.
 * Evaluate respects assigned x. No code/files/network access is interpreted. */
int pos_calc_run(pos_calc_t *, const char *, pos_calc_action_t,
                 char out[POS_CALC_TEXT]);
void pos_calc_number(double, char *, unsigned);
extern const char *const pos_calc_functions[];
extern const unsigned pos_calc_function_count;
