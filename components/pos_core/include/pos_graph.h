#pragma once
#include "pos_calc_tools.h"
enum {
  POS_GRAPH_ROOT,
  POS_GRAPH_INTERSECTION,
  POS_GRAPH_MINIMUM,
  POS_GRAPH_MAXIMUM,
  POS_GRAPH_INTEGRAL
};
/* Numerical tools on explicit finite intervals. Integral uses refined Simpson
 * with a convergence check; poles/domain failures are errors, not results. */
int pos_graph_analyze(const pos_calc_t *, const char *, const char *, unsigned,
                      double a, double b, double *x, double *y);
