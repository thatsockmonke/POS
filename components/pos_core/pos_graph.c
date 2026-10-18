#include "pos_graph.h"
#include <math.h>
#include <stddef.h>
static int value(const pos_calc_t *c, const char *f, const char *g, double x,
                 double *y) {
  if (pos_calc_sample(c, f, x, y))
    return -1;
  if (g && *g) {
    double z;
    if (pos_calc_sample(c, g, x, &z))
      return -1;
    *y -= z;
  }
  return !isfinite(*y);
}
int pos_graph_analyze(const pos_calc_t *c, const char *f, const char *g,
                      unsigned op, double a, double b, double *x, double *y) {
  if (!isfinite(a) || !isfinite(b) || a >= b || op > POS_GRAPH_INTEGRAL || !x ||
      !y)
    return -1;
  double fa, fb;
  if (value(c, f, NULL, a, &fa) || value(c, f, NULL, b, &fb))
    return -1;
  if (op == POS_GRAPH_INTEGRAL) {
    double previous = NAN;
    for (unsigned n = 32; n <= 4096; n *= 2) {
      double h = (b - a) / n, sum = fa + fb;
      for (unsigned i = 1; i < n; ++i) {
        double v;
        if (value(c, f, NULL, a + h * i, &v))
          return -1;
        sum += (i & 1 ? 4 : 2) * v;
      }
      double v = sum * h / 3;
      if (!isfinite(v))
        return -1;
      if (n >= 128 && fabs(v - previous) <= 1e-7 * fmax(1, fabs(v))) {
        *x = b;
        *y = v;
        return 0;
      }
      previous = v;
    }
    return -1;
  }
  if (op == POS_GRAPH_ROOT || op == POS_GRAPH_INTERSECTION) {
    const char *second = op == POS_GRAPH_INTERSECTION ? g : NULL;
    if (op == POS_GRAPH_INTERSECTION && (!g || !*g))
      return -1;
    if (value(c, f, second, a, &fa) || value(c, f, second, b, &fb))
      return -1;
    if (fa == 0) {
      *x = a;
      return value(c, f, NULL, a, y);
    }
    if (fb == 0) {
      *x = b;
      return value(c, f, NULL, b, y);
    }
    if (signbit(fa) == signbit(fb))
      return -1;
    double best = fmin(fabs(fa), fabs(fb));
    for (unsigned i = 0; i < 80; ++i) {
      double mid = a + (b - a) / 2, v;
      if (value(c, f, second, mid, &v))
        return -1;
      if (fabs(v) < 1e-9 && fabs(v) <= best) {
        *x = mid;
        return value(c, f, NULL, mid, y);
      }
      if (mid == a || mid == b)
        break;
      if (signbit(v) == signbit(fa)) {
        a = mid;
        fa = v;
      } else {
        b = mid;
        fb = v;
      }
    }
    return -1; /* A sign-changing pole is not a root. */
  }
  double mid = (a + b) / 2, fm;
  if (value(c, f, NULL, mid, &fm))
    return -1;
  int sign = op == POS_GRAPH_MAXIMUM ? -1 : 1;
  if (sign * fm >= sign * fa || sign * fm >= sign * fb)
    return -1;
  for (unsigned i = 0; i < 96; ++i) {
    double l = a + (b - a) * 0.381966011250105,
           r = b - (b - a) * 0.381966011250105, fl, fr;
    if (value(c, f, NULL, l, &fl) || value(c, f, NULL, r, &fr))
      return -1;
    if (sign * fl < sign * fr)
      b = r;
    else
      a = l;
  }
  *x = (a + b) / 2;
  return value(c, f, NULL, *x, y);
}
