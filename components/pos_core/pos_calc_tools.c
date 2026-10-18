#include "pos_calc_tools.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Checked exact rational arithmetic. Never silently converts an overflow into
 * a floating-point result. Values deliberately exclude INT64_MIN. */
typedef struct {
  int64_t n, d;
} rational;
typedef struct {
  const char *p;
  unsigned depth, steps;
  bool bad;
} exact_parser;
static int64_t gcd64(int64_t a, int64_t b) {
  if (a < 0)
    a = -a;
  if (b < 0)
    b = -b;
  while (b) {
    int64_t r = a % b;
    a = b;
    b = r;
  }
  return a ? a : 1;
}
static rational norm(exact_parser *p, int64_t n, int64_t d) {
  if (!d || n == INT64_MIN || d == INT64_MIN) {
    p->bad = true;
    return (rational){0, 1};
  }
  if (d < 0) {
    n = -n;
    d = -d;
  }
  int64_t g = gcd64(n, d);
  return (rational){n / g, d / g};
}
static rational ratop(exact_parser *p, rational a, rational b, char op) {
  int64_t x = 0, y = 0, n = 0, d = 1;
  if (op == '/' && !b.n) {
    p->bad = true;
    return (rational){0, 1};
  }
  if (op == '/') {
    int64_t z = b.n;
    b.n = b.d;
    b.d = z;
    op = '*';
  }
  if (op == '*') {
    int64_t g = gcd64(a.n, b.d), h = gcd64(b.n, a.d);
    if (__builtin_mul_overflow(a.n / g, b.n / h, &n) ||
        __builtin_mul_overflow(a.d / h, b.d / g, &d))
      p->bad = true;
  } else {
    int64_t g = gcd64(a.d, b.d);
    if (__builtin_mul_overflow(a.n, b.d / g, &x) ||
        __builtin_mul_overflow(b.n, a.d / g, &y) ||
        (op == '+' ? __builtin_add_overflow(x, y, &n)
                   : __builtin_sub_overflow(x, y, &n)) ||
        __builtin_mul_overflow(a.d, b.d / g, &d))
      p->bad = true;
  }
  if (p->bad)
    return (rational){0, 1};
  return norm(p, n, d);
}
static void space(exact_parser *p) {
  while (isspace((unsigned char)*p->p))
    ++p->p;
}
static rational rexpr(exact_parser *);
static rational atom(exact_parser *p) {
  space(p);
  rational a = {0, 1};
  if (++p->steps > 256 || ++p->depth > 8) {
    p->bad = true;
    --p->depth;
    return a;
  }
  if (*p->p == '(') {
    ++p->p;
    a = rexpr(p);
    space(p);
    if (*p->p != ')')
      p->bad = true;
    else
      ++p->p;
  } else {
    bool digit = false, dot = false;
    while (isdigit((unsigned char)*p->p) || (*p->p == '.' && !dot)) {
      if (*p->p == '.') {
        dot = true;
        ++p->p;
        continue;
      }
      digit = true;
      int64_t t;
      if (__builtin_mul_overflow(a.n, (int64_t)10, &t) ||
          __builtin_add_overflow(t, (int64_t)(*p->p - '0'), &a.n) ||
          (dot && __builtin_mul_overflow(a.d, (int64_t)10, &a.d))) {
        p->bad = true;
        break;
      }
      ++p->p;
    }
    if (!digit)
      p->bad = true;
  }
  --p->depth;
  return norm(p, a.n, a.d);
}
static rational unary(exact_parser *p);
static rational power(exact_parser *p) {
  rational a = atom(p);
  space(p);
  if (*p->p == '^') {
    ++p->p;
    rational b = unary(p);
    if (b.d != 1 || b.n < -63 || b.n > 63) {
      p->bad = true;
      return a;
    }
    rational r = {1, 1};
    int64_t n = b.n < 0 ? -b.n : b.n;
    while (n && !p->bad) {
      if (n & 1)
        r = ratop(p, r, a, '*');
      n >>= 1;
      if (n)
        a = ratop(p, a, a, '*');
    }
    a = b.n < 0 ? ratop(p, (rational){1, 1}, r, '/') : r;
  }
  return a;
}
static rational unary(exact_parser *p) {
  space(p);
  if (++p->depth > 8 || ++p->steps > 256) {
    p->bad = true;
    --p->depth;
    return (rational){0, 1};
  }
  rational a;
  if (*p->p == '+' || *p->p == '-') {
    char sign = *p->p++;
    a = unary(p);
    if (sign == '-')
      a.n = -a.n;
  } else
    a = power(p);
  --p->depth;
  return a;
}
static rational product(exact_parser *p) {
  rational a = unary(p);
  space(p);
  while (!p->bad && (*p->p == '*' || *p->p == '/')) {
    char c = *p->p++;
    a = ratop(p, a, unary(p), c);
    space(p);
  }
  return a;
}
static rational rexpr(exact_parser *p) {
  rational a = product(p);
  space(p);
  while (!p->bad && (*p->p == '+' || *p->p == '-')) {
    char c = *p->p++;
    a = ratop(p, a, product(p), c);
    space(p);
  }
  return a;
}
int pos_calc_exact(const char *text, char out[POS_CALC_TEXT]) {
  if (!text || strlen(text) > 160)
    return -1;
  exact_parser p = {.p = text};
  rational r = rexpr(&p);
  space(&p);
  if (p.bad || *p.p) {
    snprintf(out, POS_CALC_TEXT,
             "Exact: arithmetic only; overflow or invalid expression");
    return -1;
  }
  if (r.d == 1)
    snprintf(out, POS_CALC_TEXT, "%" PRId64, r.n);
  else
    snprintf(out, POS_CALC_TEXT, "%" PRId64 "/%" PRId64, r.n, r.d);
  return 0;
}
int pos_calc_sample(const pos_calc_t *source, const char *text, double x,
                    double *y) {
  pos_calc_t calc = *source;
  char out[POS_CALC_TEXT];
  calc.variables['x' - 'a'] = x;
  calc.assigned |= 1u << ('x' - 'a');
  /* An assignment is not a graphable function. */
  if (strchr(text, '=') || pos_calc_run(&calc, text, POS_CALC_EVALUATE, out) ||
      !isfinite(calc.ans))
    return -1;
  *y = calc.ans;
  return 0;
}
/* Compact 4x4 Gauss-Jordan with scaled partial pivoting. */
static int reduce(double a[4][8], unsigned n, unsigned cols, double *det) {
  *det = 1;
  for (unsigned i = 0; i < n; ++i) {
    unsigned pivot = i;
    double best = 0;
    for (unsigned r = i; r < n; ++r) {
      double scale = 0;
      for (unsigned c = 0; c < n; ++c)
        scale = fmax(scale, fabs(a[r][c]));
      double score = scale ? fabs(a[r][i]) / scale : 0;
      if (score > best) {
        best = score;
        pivot = r;
      }
    }
    if (best < 1e-12)
      return -1;
    if (pivot != i) {
      for (unsigned c = 0; c < cols; ++c) {
        double t = a[i][c];
        a[i][c] = a[pivot][c];
        a[pivot][c] = t;
      }
      *det = -*det;
    }
    double q = a[i][i];
    *det *= q;
    for (unsigned c = 0; c < cols; ++c)
      a[i][c] /= q;
    for (unsigned r = 0; r < n; ++r)
      if (r != i) {
        double f = a[r][i];
        for (unsigned c = 0; c < cols; ++c)
          a[r][c] -= f * a[i][c];
      }
  }
  for (unsigned r = 0; r < n; ++r)
    for (unsigned c = 0; c < cols; ++c)
      if (!isfinite(a[r][c]))
        return -1;
  return 0;
}
static int numeric(pos_calc_t *calc, const char *text, double *value) {
  pos_calc_t copy = *calc;
  char out[POS_CALC_TEXT];
  if (pos_calc_run(&copy, text, POS_CALC_EVALUATE, out))
    return -1;
  *value = copy.ans;
  return !isfinite(*value) ? -1 : 0;
}
/* Explicit linear-term grammar, rather than sampling arbitrary functions and
 * accidentally accepting a nonlinear system. Coefficients may be decimals or
 * simple fractions; xyzw are unknowns, with explicit multiplication. */
static int linear_side(const char *p, unsigned n, double row[8], double side) {
  bool first = true;
  while (*p) {
    double sign = 1;
    if (*p == '+' || *p == '-') {
      if (*p++ == '-')
        sign = -1;
    } else if (!first)
      return -1;
    first = false;
    if (!*p)
      return -1;
    char *end;
    double value = 1;
    if (isdigit((unsigned char)*p) || *p == '.') {
      errno = 0;
      value = strtod(p, &end);
      if (p == end || errno || !isfinite(value))
        return -1;
      p = end;
      if (*p == '/') {
        ++p;
        double divisor = strtod(p, &end);
        if (p == end || !isfinite(divisor) || !divisor)
          return -1;
        value /= divisor;
        p = end;
      }
      if (*p == '*') {
        ++p;
        if (!*p || !strchr("xyzw", *p))
          return -1;
      }
    } else if (!strchr("xyzw", *p))
      return -1;
    const char *variable = *p ? strchr("xyzw", *p) : NULL;
    if (variable) {
      unsigned col = (unsigned)(variable - "xyzw");
      if (col >= n)
        return -1;
      row[col] += side * sign * value;
      ++p;
    } else
      row[n] -= side * sign * value;
    if (*p && *p != '+' && *p != '-')
      return -1;
  }
  return first ? -1 : 0;
}

int pos_calc_system(pos_calc_t *source, const char *text,
                    char out[POS_CALC_TEXT]) {
  char work[161];
  double a[4][8] = {{0}};
  unsigned n = 0;
  if (!text || strlen(text) > 160)
    return -1;
  strcpy(work, text);
  /* Explicit augmented rows avoid pretending numeric sampling proves linearity.
   * Each row: coefficients of x,y,z,w then RHS; 2..4 square system. */
  for (char *p = work; *p; ++p)
    if (*p == ';')
      ++n;
  ++n;
  if (n < 2 || n > 4)
    goto bad;
  char *rows[4], *save = NULL;
  unsigned nr = 0;
  for (char *p = strtok_r(work, ";", &save); p && nr < 4;
       p = strtok_r(NULL, ";", &save))
    rows[nr++] = p;
  if (nr != n)
    goto bad;
  if (strchr(text, '=')) {
    for (unsigned r = 0; r < n; ++r) {
      char clean[161];
      size_t at = 0;
      for (const char *p = rows[r]; *p; ++p)
        if (!isspace((unsigned char)*p))
          clean[at++] = *p;
      clean[at] = 0;
      char *eq = strchr(clean, '=');
      if (!eq || strchr(eq + 1, '='))
        goto bad;
      *eq++ = 0;
      if (linear_side(clean, n, a[r], 1) || linear_side(eq, n, a[r], -1))
        goto bad;
    }
  } else {
    for (unsigned r = 0; r < n; ++r) {
      bool cell = true;
      for (const char *p = rows[r];; ++p) {
        if (isspace((unsigned char)*p))
          continue;
        if (*p == ',' || !*p) {
          if (cell)
            goto bad;
          cell = true;
          if (!*p)
            break;
        } else
          cell = false;
      }
      char *s = NULL;
      unsigned c = 0;
      for (char *v = strtok_r(rows[r], ",", &s); v;
           v = strtok_r(NULL, ",", &s)) {
        if (c > n || numeric(source, v, &a[r][c++]))
          goto bad;
      }
      if (c != n + 1)
        goto bad;
    }
  }
  double det;
  if (reduce(a, n, n + 1, &det)) {
    snprintf(out, POS_CALC_TEXT,
             "No unique solution (singular or ill-conditioned)");
    return -1;
  }
  size_t used = 0;
  const char *names = "xyzw";
  for (unsigned i = 0; i < n; ++i) {
    char number[40];
    pos_calc_number(a[i][n], number, sizeof(number));
    int k = snprintf(out + used, POS_CALC_TEXT - used, "%s%c=%s", i ? "; " : "",
                     names[i], number);
    if (k < 0 || (size_t)k >= POS_CALC_TEXT - used)
      goto bad;
    used += k;
  }
  return 0;
bad:
  snprintf(
      out, POS_CALC_TEXT,
      "Use linear equations: 2*x+y=3;x-y=0 or augmented rows: 2,1,3;1,-1,0");
  return -1;
}
static bool empty_cell(const char *text) {
  bool start = true;
  for (const char *p = text;; ++p) {
    if (isspace((unsigned char)*p))
      continue;
    if (*p == ',' || *p == ';' || !*p) {
      if (start)
        return true;
      start = true;
      if (!*p)
        break;
    } else
      start = false;
  }
  return false;
}
static int matrix(const char *text, double m[4][8], unsigned *rows,
                  unsigned *cols) {
  char work[161];
  if (!text || strlen(text) > 160 || empty_cell(text))
    return -1;
  strcpy(work, text);
  *rows = *cols = 0;
  char *rs = NULL;
  for (char *r = strtok_r(work, ";", &rs); r; r = strtok_r(NULL, ";", &rs)) {
    if (*rows == 4)
      return -1;
    unsigned n = 0;
    char *cs = NULL;
    for (char *v = strtok_r(r, ",", &cs); v; v = strtok_r(NULL, ",", &cs)) {
      char *end;
      if (n == 4)
        return -1;
      errno = 0;
      double x = strtod(v, &end);
      while (isspace((unsigned char)*end))
        ++end;
      if (errno || v == end || *end || !isfinite(x))
        return -1;
      m[*rows][n++] = x;
    }
    if (!n || (*rows && n != *cols))
      return -1;
    *cols = n;
    ++*rows;
  }
  return *rows ? 0 : -1;
}
int pos_calc_matrix(const char *op, const char *ta, const char *tb,
                    char out[POS_CALC_TEXT]) {
  double a[4][8] = {{0}}, b[4][8] = {{0}}, r[4][8] = {{0}};
  unsigned ar, ac, br = 0, bc = 0, rr, rc;
  if (matrix(ta, a, &ar, &ac))
    goto bad;
  rr = ar;
  rc = ac;
  if (!strcmp(op, "det") || !strcmp(op, "inv")) {
    if (ar != ac)
      goto bad;
    for (unsigned i = 0; i < ar; ++i)
      a[i][ac + i] = 1;
    double det;
    if (reduce(a, ar, 2 * ac, &det)) {
      if (!strcmp(op, "det")) {
        strcpy(out, "0 (singular)");
        return 0;
      }
      goto bad;
    }
    if (!strcmp(op, "det")) {
      pos_calc_number(det, out, POS_CALC_TEXT);
      return 0;
    }
    for (unsigned i = 0; i < ar; ++i)
      for (unsigned j = 0; j < ac; ++j)
        r[i][j] = a[i][ac + j];
  } else {
    if (matrix(tb, b, &br, &bc))
      goto bad;
    if (!strcmp(op, "dot")) {
      if (ar != 1 || br != 1 || ac != bc)
        goto bad;
      double d = 0;
      for (unsigned i = 0; i < ac; ++i)
        d += a[0][i] * b[0][i];
      if (!isfinite(d))
        goto bad;
      pos_calc_number(d, out, POS_CALC_TEXT);
      return 0;
    } else if (!strcmp(op, "mul")) {
      if (ac != br)
        goto bad;
      rc = bc;
      for (unsigned i = 0; i < ar; ++i)
        for (unsigned j = 0; j < bc; ++j)
          for (unsigned k = 0; k < ac; ++k)
            r[i][j] += a[i][k] * b[k][j];
    } else if (!strcmp(op, "add") || !strcmp(op, "sub")) {
      if (ar != br || ac != bc)
        goto bad;
      for (unsigned i = 0; i < ar; ++i)
        for (unsigned j = 0; j < ac; ++j)
          r[i][j] = a[i][j] + (!strcmp(op, "sub") ? -b[i][j] : b[i][j]);
    } else
      goto bad;
  }
  size_t used = 0;
  for (unsigned i = 0; i < rr; ++i)
    for (unsigned j = 0; j < rc; ++j) {
      if (!isfinite(r[i][j]))
        goto bad;
      char v[32];
      pos_calc_number(r[i][j], v, sizeof(v));
      int k = snprintf(out + used, POS_CALC_TEXT - used, "%s%s",
                       j   ? ","
                       : i ? "; "
                           : "",
                       v);
      if (k < 0 || (size_t)k >= POS_CALC_TEXT - used)
        goto bad;
      used += k;
    }
  return 0;
bad:
  snprintf(out, POS_CALC_TEXT,
           "Invalid dimensions, singular matrix or range limit (max 4x4)");
  return -1;
}
/* Dimensional unit signatures: length, mass, time; temperature affine. */
typedef struct {
  const char *name;
  double factor, offset;
  int l, m, t, k;
} unit;
static const unit units[] = {{"m", 1, 0, 1, 0, 0, 0},
                             {"km", 1000, 0, 1, 0, 0, 0},
                             {"cm", .01, 0, 1, 0, 0, 0},
                             {"mm", .001, 0, 1, 0, 0, 0},
                             {"mi", 1609.344, 0, 1, 0, 0, 0},
                             {"ft", .3048, 0, 1, 0, 0, 0},
                             {"in", .0254, 0, 1, 0, 0, 0},
                             {"yd", .9144, 0, 1, 0, 0, 0},
                             {"kg", 1, 0, 0, 1, 0, 0},
                             {"g", .001, 0, 0, 1, 0, 0},
                             {"lb", .45359237, 0, 0, 1, 0, 0},
                             {"oz", .028349523125, 0, 0, 1, 0, 0},
                             {"s", 1, 0, 0, 0, 1, 0},
                             {"min", 60, 0, 0, 0, 1, 0},
                             {"h", 3600, 0, 0, 0, 1, 0},
                             {"m/s", 1, 0, 1, 0, -1, 0},
                             {"km/h", 1 / 3.6, 0, 1, 0, -1, 0},
                             {"mph", .44704, 0, 1, 0, -1, 0},
                             {"m2", 1, 0, 2, 0, 0, 0},
                             {"km2", 1e6, 0, 2, 0, 0, 0},
                             {"acre", 4046.8564224, 0, 2, 0, 0, 0},
                             {"L", .001, 0, 3, 0, 0, 0},
                             {"mL", 1e-6, 0, 3, 0, 0, 0},
                             {"gal", .003785411784, 0, 3, 0, 0, 0},
                             {"K", 1, 0, 0, 0, 0, 1},
                             {"C", 1, 273.15, 0, 0, 0, 1},
                             {"F", 5. / 9, 255.3722222222222, 0, 0, 0, 1}};
int pos_calc_units(double value, const char *from, const char *to,
                   double *out) {
  const unit *a = NULL, *b = NULL;
  for (unsigned i = 0; i < sizeof(units) / sizeof(*units); ++i) {
    if (!strcmp(from, units[i].name))
      a = &units[i];
    if (!strcmp(to, units[i].name))
      b = &units[i];
  }
  if (!a || !b || a->l != b->l || a->m != b->m || a->t != b->t ||
      a->k != b->k || !isfinite(value))
    return -1;
  *out = (value * a->factor + a->offset - b->offset) / b->factor;
  return isfinite(*out) ? 0 : -1;
}
int pos_calc_bits(const char *text, unsigned width, char out[POS_CALC_TEXT]) {
  if (!text || strlen(text) > 160 ||
      (width != 8 && width != 16 && width != 32 && width != 64))
    return -1;
  char work[161], *end;
  strcpy(work, text);
  char *p = work;
  while (isspace((unsigned char)*p))
    ++p;
  bool invert = *p == '~';
  if (invert)
    ++p;
  if (*p == '-')
    goto bad;
  errno = 0;
  uint64_t a;
  if (!strncmp(p, "0b", 2)) {
    p += 2;
    a = 0;
    unsigned n = 0;
    while (*p == '0' || *p == '1') {
      if (++n > 64)
        goto bad;
      a = (a << 1) | (*p++ - '0');
    }
    if (!n)
      goto bad;
    end = p;
  } else {
    a = strtoull(p, &end, 0);
    if (p == end || errno)
      goto bad;
  }
  uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
  a &= mask;
  p = end;
  while (isspace((unsigned char)*p))
    ++p;
  if (invert) {
    a = ~a;
    if (*p)
      goto bad;
  } else if (*p) {
    char op[3] = {0};
    op[0] = *p++;
    if ((op[0] == '<' || op[0] == '>') && *p == op[0])
      op[1] = *p++;
    while (isspace((unsigned char)*p))
      ++p;
    if (*p == '-')
      goto bad;
    errno = 0;
    uint64_t b = strtoull(p, &end, 0);
    if (p == end || errno)
      goto bad;
    while (isspace((unsigned char)*end))
      ++end;
    if (*end)
      goto bad;
    if (!strcmp(op, "&"))
      a &= b;
    else if (!strcmp(op, "|"))
      a |= b;
    else if (!strcmp(op, "^"))
      a ^= b;
    else if (!strcmp(op, "<<") && b < width)
      a <<= b;
    else if (!strcmp(op, ">>") && b < width)
      a >>= b;
    else
      goto bad;
  }
  a &= mask;
  char binary[65];
  for (unsigned i = 0; i < width; ++i)
    binary[i] = (a >> (width - i - 1) & 1) + '0';
  binary[width] = 0;
  snprintf(out, POS_CALC_TEXT, "%" PRIu64 " / 0x%0*" PRIX64 " / 0b%s", a,
           width / 4, a, binary);
  return 0;
bad:
  snprintf(out, POS_CALC_TEXT,
           "Use unsigned integer [&,|,^,<<,>> integer] or ~integer; width "
           "8/16/32/64");
  return -1;
}
