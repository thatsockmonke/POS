#pragma once
#include "pos_calculator.h"
/* All operations are bounded; failures leave the calculator state unchanged. */
int pos_calc_exact(const char *, char out[POS_CALC_TEXT]);
int pos_calc_system(pos_calc_t *, const char *, char out[POS_CALC_TEXT]);
int pos_calc_matrix(const char *operation, const char *a, const char *b,
                    char out[POS_CALC_TEXT]);
int pos_calc_units(double value, const char *from, const char *to, double *out);
int pos_calc_bits(const char *, unsigned width, char out[POS_CALC_TEXT]);
int pos_calc_sample(const pos_calc_t *, const char *, double x, double *y);
