#include "pos_calculator.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
#define N (POS_CALC_DEGREE + 1)
typedef struct {
  double c[N];
} poly;
typedef struct {
  const char *p;
  pos_calc_t *calc;
  unsigned depth, steps;
  bool symbolic;
  const char *error;
} parser;
const char *const pos_calc_functions[] = {
    "sin",  "cos",   "tan",   "asin",  "acos",  "atan",  "sinh",  "cosh",
    "tanh", "asinh", "acosh", "atanh", "ln",    "log",   "exp",   "sqrt",
    "cbrt", "abs",   "floor", "ceil",  "round", "trunc", "fact",  "ncr",
    "npr",  "gcd",   "lcm",   "mod",   "min",   "max",   "hypot", "atan2",
    "root", "pow",   "deg",   "rad",   "mean",  "std"};
const unsigned pos_calc_function_count =
    sizeof(pos_calc_functions) / sizeof(*pos_calc_functions);
static poly expression(parser *);
static void skip(parser *p) {
  while (isspace((unsigned char)*p->p))
    ++p->p;
}
static poly constant(double x) {
  poly a = {{0}};
  a.c[0] = x;
  return a;
}
static int degree(poly a) {
  for (int i = N - 1; i > 0; --i)
    if (a.c[i] != 0)
      return i;
  return 0;
}
static poly bad(parser *p, const char *why) {
  if (!p->error)
    p->error = why;
  return constant(0);
}
static poly multiply(parser *p, poly a, poly b) {
  poly r = {{0}};
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j)
      if (a.c[i] && b.c[j]) {
        if (i + j >= N)
          return bad(p, "Polynomial degree limit is 8");
        r.c[i + j] += a.c[i] * b.c[j];
      }
  return r;
}
static double factorial(double x) {
  if (x < 0 || x > 170 || x != floor(x))
    return NAN;
  double r = 1;
  for (int i = 2; i <= x; ++i)
    r *= i;
  return r;
}
static double realpow(double a, double b) { /* Reuse existing exp/log and keep
                                               negative integer powers real. */
  if (b == floor(b) && fabs(b) <= 2147483647.) {
    unsigned power = (unsigned)fabs(b);
    double base = b < 0 ? 1 / a : a, r = 1;
    while (power) {
      if (power & 1)
        r *= base;
      power >>= 1;
      if (power)
        base *= base;
    }
    return r;
  }
  if (a == 0)
    return b > 0 ? 0 : b == 0 ? 1 : NAN;
  if (a < 0) {
    if (b != floor(b) || fabs(b) > 9007199254740991.)
      return NAN;
    double r = exp(b * log(-a));
    return fmod(fabs(b), 2) ? -r : r;
  }
  return exp(b * log(a));
}
static double gcd(double a, double b) {
  if (a != floor(a) || b != floor(b) || fabs(a) > 9007199254740991. ||
      fabs(b) > 9007199254740991.)
    return NAN;
  a = fabs(a);
  b = fabs(b);
  while (b) {
    double r = fmod(a, b);
    a = b;
    b = r;
  }
  return a;
}
static double function(parser *p, unsigned fn, double a, double b,
                       unsigned argc) {
  double angle = p->calc->degrees ? PI / 180 : 1, r = NAN;
  if (fn < 23 || fn == 34 || fn == 35) {
    if (argc != 1)
      return NAN;
  } else if (argc != 2)
    return NAN;
  switch (fn) {
  case 0:
    r = sin(a * angle);
    break;
  case 1:
    r = cos(a * angle);
    break;
  case 2: {
    double c = cos(a * angle);
    r = fabs(c) < 1e-15 ? NAN : sin(a * angle) / c;
    break;
  }
  case 3:
    r = fabs(a) > 1 ? NAN : atan2(a, sqrt(1 - a * a)) / angle;
    break;
  case 4:
    r = fabs(a) > 1 ? NAN : atan2(sqrt(1 - a * a), a) / angle;
    break;
  case 5:
    r = atan2(a, 1) / angle;
    break;
  case 6:
    r = (exp(a) - exp(-a)) / 2;
    break;
  case 7:
    r = (exp(a) + exp(-a)) / 2;
    break;
  case 8: {
    double x = exp(-2 * fabs(a));
    r = (a < 0 ? -1 : 1) * (1 - x) / (1 + x);
    break;
  }
  case 9:
    r = (a < 0 ? -1 : 1) * log(fabs(a) + sqrt(a * a + 1));
    break;
  case 10:
    r = a < 1 ? NAN : log(a + sqrt(a * a - 1));
    break;
  case 11:
    r = fabs(a) >= 1 ? NAN : log((1 + a) / (1 - a)) / 2;
    break;
  case 12:
    r = log(a);
    break;
  case 13:
    r = log(a) / log(10);
    break;
  case 14:
    r = exp(a);
    break;
  case 15:
    r = sqrt(a);
    break;
  case 16:
    r = a == 0 ? 0 : (a < 0 ? -1 : 1) * exp(log(fabs(a)) / 3);
    break;
  case 17:
    r = fabs(a);
    break;
  case 18:
    r = floor(a);
    break;
  case 19:
    r = ceil(a);
    break;
  case 20:
    r = round(a);
    break;
  case 21:
    r = trunc(a);
    break;
  case 22:
    r = factorial(a);
    break;
  case 23:
  case 24: {
    if (a < 0 || b < 0 || a != floor(a) || b != floor(b) || b > a || a > 170)
      break;
    if (fn == 23 && b > a - b)
      b = a - b;
    r = 1;
    for (unsigned i = 0; i < b; ++i)
      r = r * (a - i) / (fn == 23 ? i + 1 : 1);
    break;
  }
  case 25:
    r = gcd(a, b);
    break;
  case 26: {
    double d = gcd(a, b);
    r = d == 0 ? 0 : fabs(a / d * b);
    break;
  }
  case 27:
    r = fmod(a, b);
    break;
  case 28:
    r = fmin(a, b);
    break;
  case 29:
    r = fmax(a, b);
    break;
  case 30: {
    double scale = fmax(fabs(a), fabs(b));
    r = scale ? scale *
                    sqrt((a / scale) * (a / scale) + (b / scale) * (b / scale))
              : 0;
    break;
  }
  case 31:
    r = atan2(a, b) / angle;
    break;
  case 32:
    if (b && !(a < 0 && (b != floor(b) || fmod(fabs(b), 2) != 1)))
      r = (a < 0 ? -1 : 1) * realpow(fabs(a), 1 / b);
    break;
  case 33:
    r = realpow(a, b);
    break;
  case 34:
    r = a * 180 / PI;
    break;
  case 35:
    r = a * PI / 180;
    break;
  default:
    break;
  }
  return r;
}
static poly primary(parser *p) {
  skip(p);
  if (++p->steps > 256 || ++p->depth > 8) {
    --p->depth;
    return bad(p, "Expression too complex (8 levels)");
  }
  poly a = constant(0);
  if (*p->p == '(') {
    ++p->p;
    a = expression(p);
    skip(p);
    if (*p->p != ')')
      a = bad(p, "Missing closing parenthesis");
    else
      ++p->p;
  } else if (isdigit((unsigned char)*p->p) || *p->p == '.') {
    char *end;
    a = constant(strtod(p->p, &end));
    if (end == p->p)
      a = bad(p, "Invalid number");
    p->p = end;
  } else if (isalpha((unsigned char)*p->p)) {
    char name[12];
    unsigned n = 0;
    while (isalnum((unsigned char)*p->p)) {
      if (n + 1 < sizeof(name))
        name[n++] = (char)tolower((unsigned char)*p->p);
      ++p->p;
    }
    name[n] = 0;
    skip(p);
    unsigned fn = 0;
    while (fn < pos_calc_function_count && strcmp(name, pos_calc_functions[fn]))
      ++fn;
    if (fn < pos_calc_function_count) {
      if (*p->p != '(')
        a = bad(p, "Functions need parentheses");
      else {
        ++p->p;
        poly arg = expression(p);
        double values[16];
        unsigned count = 0;
        if (degree(arg))
          a = bad(p, "Use a polynomial for this CAS operation");
        values[count++] = arg.c[0];
        skip(p);
        while (*p->p == ',' && !p->error) {
          ++p->p;
          arg = expression(p);
          if (degree(arg) || count == 16) {
            a = bad(p, "Up to 16 numeric arguments");
            break;
          }
          values[count++] = arg.c[0];
          skip(p);
        }
        if (*p->p != ')')
          a = bad(p, "Missing closing parenthesis");
        else
          ++p->p;
        for (unsigned i = 0; i < count; ++i)
          if (!isfinite(values[i]))
            bad(p, "Non-finite argument");
        if (!p->error) {
          double r;
          if (fn >= 36) {
            double mean = 0, m2 = 0;
            for (unsigned i = 0; i < count; ++i) {
              double d = values[i] - mean;
              mean += d / (i + 1);
              m2 += d * (values[i] - mean);
            }
            r = fn == 36 ? mean : count > 1 ? sqrt(m2 / (count - 1)) : NAN;
          } else
            r = function(p, fn, values[0], count > 1 ? values[1] : 0, count);
          a = constant(r);
        }
      }
    } else if (!strcmp(name, "pi"))
      a = constant(PI);
    else if (!strcmp(name, "tau"))
      a = constant(2 * PI);
    else if (!strcmp(name, "e"))
      a = constant(exp(1));
    else if (!strcmp(name, "ans"))
      a = constant(p->calc->ans);
    else if (n == 1 && name[0] >= 'a' && name[0] <= 'z') {
      unsigned i = name[0] - 'a';
      if (name[0] == 'x' && (p->symbolic || !(p->calc->assigned & (1u << i))))
        a.c[1] = 1;
      else if (p->calc->assigned & (1u << i))
        a = constant(p->calc->variables[i]);
      else
        a = bad(p, "Set this variable in Variables first");
    } else
      a = bad(p, "Unknown function or variable");
  } else
    a = bad(p, "Expected a number, variable or parenthesis");
  skip(p);
  while (*p->p == '!' || *p->p == '%') {
    int op = *p->p++;
    if (op == '!' && degree(a))
      a = bad(p, "Factorial needs a number");
    else if (op == '!')
      a.c[0] = factorial(a.c[0]);
    else
      for (int i = 0; i < N; ++i)
        a.c[i] /= 100;
    skip(p);
  }
  --p->depth;
  return a;
}
static poly unary(parser *);
static poly power(parser *p) {
  poly a = primary(p);
  skip(p);
  if (*p->p == '^') {
    ++p->p;
    if (++p->depth > 8) {
      --p->depth;
      return bad(p, "Too many nested exponents");
    }
    poly b = unary(p);
    --p->depth;
    if (degree(b))
      return bad(p, "Exponent must be numeric");
    if (!degree(a))
      return constant(realpow(a.c[0], b.c[0]));
    if (b.c[0] < 0 || b.c[0] > 8 || b.c[0] != floor(b.c[0]))
      return bad(p, "Polynomial exponent must be 0..8");
    poly r = constant(1);
    for (int i = 0; i < b.c[0] && !p->error; ++i)
      r = multiply(p, r, a);
    a = r;
  }
  return a;
}
static poly unary(parser *p) {
  skip(p);
  if (*p->p == '+' || *p->p == '-') {
    int sign = *p->p++;
    if (++p->depth > 8) {
      --p->depth;
      return bad(p, "Too many signs");
    }
    poly a = unary(p);
    --p->depth;
    if (sign == '-')
      for (int i = 0; i < N; ++i)
        a.c[i] = -a.c[i];
    return a;
  }
  return power(p);
}
static poly product(parser *p) {
  poly a = unary(p);
  for (;;) {
    skip(p);
    int op = *p->p;
    if (op == '*' || op == '/')
      ++p->p;
    else if (op == '(' || isalpha((unsigned char)op))
      op = '*';
    else
      break;
    poly b = unary(p);
    if (op == '*')
      a = multiply(p, a, b);
    else {
      if (degree(b) || !b.c[0])
        return bad(p, "Division needs a nonzero numeric denominator");
      for (int i = 0; i < N; ++i)
        a.c[i] /= b.c[0];
    }
    if (p->error)
      break;
  }
  return a;
}
static poly expression(parser *p) {
  poly a = product(p);
  for (;;) {
    skip(p);
    int op = *p->p;
    if (op != '+' && op != '-')
      break;
    ++p->p;
    poly b = product(p);
    for (int i = 0; i < N; ++i)
      a.c[i] += op == '+' ? b.c[i] : -b.c[i];
    if (p->error)
      break;
  }
  return a;
}
void pos_calc_number(double x, char *out, unsigned cap) {
  if (x == 0)
    x = 0;
  snprintf(out, cap, "%.12g", x);
  if (!isfinite(x) || fabs(x) > 1e9 || fabs(x) < 1e-12)
    return;
  double a = fabs(x);
  long long h0 = 0, h1 = 1, k0 = 1, k1 = 0;
  for (unsigned i = 0; i < 24; ++i) {
    double whole = floor(a);
    if (whole > 1e9)
      break;
    if (whole && h1 > (1000000000000000LL - h0) / (long long)whole)
      break;
    long long h = (long long)whole * h1 + h0, k = (long long)whole * k1 + k0;
    if (k > 1000000 || h > 1000000000000000LL)
      break;
    if (k > 1 && fabs((double)h / k - fabs(x)) < 1e-12 * fmax(1, fabs(x))) {
      snprintf(out, cap, "%s%lld/%lld", x < 0 ? "-" : "", h, k);
      return;
    }
    h0 = h1;
    h1 = h;
    k0 = k1;
    k1 = k;
    double r = a - whole;
    if (r < 1e-15)
      break;
    a = 1 / r;
  }
}
static int show_poly(poly a, char out[POS_CALC_TEXT]) {
  out[0] = 0;
  for (int i = degree(a); i >= 0; --i) {
    double v = a.c[i];
    if (!v)
      continue;
    char n[48], part[80];
    pos_calc_number(fabs(v), n, sizeof(n));
    snprintf(part, sizeof(part), "%s%s%s%s",
             *out    ? (v < 0 ? " - " : " + ")
             : v < 0 ? "-"
                     : "",
             i && fabs(v) == 1 ? "" : n, i && fabs(v) != 1 ? "*" : "",
             i ? "x" : "");
    size_t used = strlen(out);
    int written = snprintf(out + used, POS_CALC_TEXT - used, "%s", part);
    if (written < 0 || (unsigned)written >= POS_CALC_TEXT - used)
      return -1;
    if (i > 1) {
      used = strlen(out);
      written = snprintf(out + used, POS_CALC_TEXT - used, "^%d", i);
      if (written < 0 || (unsigned)written >= POS_CALC_TEXT - used)
        return -1;
    }
  }
  if (!*out)
    strcpy(out, "0");
  return 0;
}
/* Isolate real polynomial roots using derivative critical points, including
 * repeated roots. Degree <=8 bounds recursion and work. Higher-degree complex
 * roots are deliberately not represented by this real solver. */
static double polynomial_value(const double *c, unsigned degree, double x) {
  double y = c[degree];
  while (degree)
    y = y * x + c[--degree];
  return y;
}
static bool near_root(const double *c, unsigned degree, double x, double y) {
  double scale = fabs(c[degree]);
  while (degree)
    scale = scale * fabs(x) + fabs(c[--degree]);
  return isfinite(y) && isfinite(scale) &&
         fabs(y) <= fmax(1e-14, scale * 1e-12);
}
static int real_roots(const double *c, unsigned d, double roots[8]) {
  if (d == 1) {
    roots[0] = -c[0] / c[1];
    return isfinite(roots[0]) ? 1 : -1;
  }
  double derivative[9] = {0}, critical[8], points[10];
  for (unsigned i = 1; i <= d; ++i)
    derivative[i - 1] = i * c[i];
  int count = real_roots(derivative, d - 1, critical);
  if (count < 0)
    return -1;
  double bound = 1;
  for (unsigned i = 0; i < d; ++i)
    bound = fmax(bound, 1 + fabs(c[i] / c[d]));
  if (!isfinite(bound) || bound > 1e30)
    return -1;
  unsigned n = 0;
  points[n++] = -bound;
  for (int i = 0; i < count; ++i)
    if (critical[i] > -bound && critical[i] < bound)
      points[n++] = critical[i];
  points[n++] = bound;
  unsigned found = 0;
  for (unsigned i = 0; i < n; ++i) {
    double x = points[i], y = polynomial_value(c, d, x);
    if (!isfinite(y))
      return -1;
    if (near_root(c, d, x, y) && found < d)
      roots[found++] = x;
    if (i + 1 == n)
      continue;
    double hi = points[i + 1], hy = polynomial_value(c, d, hi);
    if (!isfinite(hy))
      return -1;
    if (near_root(c, d, x, y) || near_root(c, d, hi, hy) ||
        signbit(y) == signbit(hy))
      continue;
    double lo = x;
    for (unsigned step = 0; step < 100; ++step) {
      double mid = lo + (hi - lo) / 2, my = polynomial_value(c, d, mid);
      if (!isfinite(my))
        return -1;
      if (signbit(my) == signbit(y))
        lo = mid;
      else
        hi = mid;
    }
    if (found < d)
      roots[found++] = lo + (hi - lo) / 2;
  }
  return (int)found;
}

int pos_calc_run(pos_calc_t *calc, const char *text, pos_calc_action_t action,
                 char out[POS_CALC_TEXT]) {
  if (!calc || !text || strlen(text) > 160 || action > POS_CALC_INTEGRATE) {
    snprintf(out, POS_CALC_TEXT, "Invalid expression");
    return -1;
  }
  parser p = {.p = text, .calc = calc, .symbolic = action != POS_CALC_EVALUATE};
  skip(&p);
  int variable = -1;
  if (action == POS_CALC_EVALUATE && isalpha((unsigned char)p.p[0]) &&
      p.p[1] == '=' && tolower((unsigned char)p.p[0]) != 'e') {
    variable = tolower((unsigned char)p.p[0]) - 'a';
    p.p += 2;
  }
  poly a = expression(&p);
  skip(&p);
  if (*p.p == '=' && variable < 0) {
    if (action != POS_CALC_SOLVE)
      bad(&p, "Use CAS > Solve for equations");
    ++p.p;
    poly b = expression(&p);
    for (int i = 0; i < N; ++i)
      a.c[i] -= b.c[i];
    skip(&p);
  }
  if (*p.p && !p.error)
    p.error = "Unexpected text: check operators";
  for (int i = 0; i < N; ++i)
    if (!isfinite(a.c[i]))
      p.error = "Math domain error or overflow";
  int d = degree(a);
  if (variable >= 0 && d)
    p.error = "Assign a numeric value, not an expression in x";
  if (p.error) {
    snprintf(out, POS_CALC_TEXT, "%s", p.error);
    return -1;
  }
  if (action == POS_CALC_SOLVE) {
    if (!d) {
      snprintf(out, POS_CALC_TEXT, "%s",
               a.c[0] == 0 ? "All x satisfy this equation" : "No solution");
      return 0;
    }
    if (d > 2) {
      double roots[8];
      int count = real_roots(a.c, (unsigned)d, roots);
      if (count < 0) {
        snprintf(out, POS_CALC_TEXT, "Root range/conditioning limit");
        return -1;
      }
      snprintf(out, POS_CALC_TEXT, "Real roots (numeric): ");
      size_t used = strlen(out);
      if (!count) {
        strcpy(out, "No real roots (higher-degree complex roots not solved)");
        return 0;
      }
      for (int i = 0; i < count; ++i) {
        int n = snprintf(out + used, POS_CALC_TEXT - used, "%s%.10g",
                         i ? ", " : "", roots[i]);
        if (n < 0 || (size_t)n >= POS_CALC_TEXT - used)
          return -1;
        used += n;
      }
      return 0;
    }
    double scale = fmax(fabs(a.c[0]), fmax(fabs(a.c[1]), fabs(a.c[2])));
    for (int i = 0; i <= d; ++i)
      a.c[i] /= scale;
    double roots[2];
    unsigned count = 1;
    roots[0] = -a.c[0] / a.c[1];
    if (d == 2) {
      double discriminant = a.c[1] * a.c[1] - 4 * a.c[2] * a.c[0];
      if (discriminant < 0) {
        double real = -a.c[1] / (2 * a.c[2]),
               im = sqrt(-discriminant) / (2 * fabs(a.c[2]));
        char r[48], i[48];
        pos_calc_number(real, r, sizeof(r));
        pos_calc_number(im, i, sizeof(i));
        snprintf(out, POS_CALC_TEXT, "x = %s +/- %s*i", r, i);
        return 0;
      }
      double q = -.5 * (a.c[1] + copysign(sqrt(discriminant), a.c[1]));
      roots[0] = q / a.c[2];
      if (q) {
        roots[1] = a.c[0] / q;
        count = discriminant == 0 ? 1 : 2;
      } else
        roots[0] = 0;
    }
    for (unsigned i = 0; i < count; ++i)
      if (!isfinite(roots[i])) {
        snprintf(out, POS_CALC_TEXT, "Solution exceeds numeric range");
        return -1;
      }
    char first[48], second[48];
    pos_calc_number(roots[0], first, sizeof(first));
    if (count == 2) {
      pos_calc_number(roots[1], second, sizeof(second));
      snprintf(out, POS_CALC_TEXT, "x = %s or %s", first, second);
    } else
      snprintf(out, POS_CALC_TEXT, "x = %s", first);
    return 0;
  }
  if (action == POS_CALC_DIFFERENTIATE) {
    for (int i = 0; i < N - 1; ++i)
      a.c[i] = (i + 1) * a.c[i + 1];
    a.c[N - 1] = 0;
  }
  if (action == POS_CALC_INTEGRATE) {
    if (d == N - 1) {
      snprintf(out, POS_CALC_TEXT, "Integral exceeds degree 8");
      return -1;
    }
    for (int i = N - 1; i > 0; --i)
      a.c[i] = a.c[i - 1] / i;
    a.c[0] = 0;
  }
  for (int i = 0; i < N; ++i)
    if (!isfinite(a.c[i])) {
      snprintf(out, POS_CALC_TEXT, "Result exceeds numeric range");
      return -1;
    }
  if (variable >= 0) {
    calc->variables[variable] = a.c[0];
    calc->assigned |= 1u << variable;
  }
  if (!degree(a)) {
    calc->ans = a.c[0];
    pos_calc_number(a.c[0], out, POS_CALC_TEXT);
    char decimal[48];
    snprintf(decimal, sizeof(decimal), "%.12g", a.c[0]);
    if (strchr(out, '/')) {
      size_t n = strlen(out);
      snprintf(out + n, POS_CALC_TEXT - n, " = %s", decimal);
    }
  } else if (show_poly(a, out)) {
    snprintf(out, POS_CALC_TEXT, "Polynomial answer exceeds display limit");
    return -1;
  }
  if (action == POS_CALC_INTEGRATE) {
    size_t n = strlen(out);
    if (n + 4 >= POS_CALC_TEXT) {
      snprintf(out, POS_CALC_TEXT, "Polynomial answer exceeds display limit");
      return -1;
    }
    snprintf(out + n, POS_CALC_TEXT - n, " + C");
  }
  return 0;
}
