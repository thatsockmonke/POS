#include "pos_timezone.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
const pos_timezone_t pos_timezones[] = {
    {"UTC", "UTC0"},
    {"Los Angeles / Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"New York / Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"Keep current timezone", NULL},
    {"Denver / Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"Chicago / Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"Phoenix (no DST)", "MST7"},
    {"Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Honolulu", "HST10"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Paris / Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Helsinki / Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Istanbul", "TRT-3"},
    {"Dubai", "GST-4"},
    {"Kolkata", "IST-5:30"},
    {"Kathmandu", "NPT-5:45"},
    {"Dhaka", "BST-6"},
    {"Bangkok", "ICT-7"},
    {"Singapore / Beijing", "SGT-8"},
    {"Tokyo / Seoul", "JST-9"},
    {"Darwin", "ACST-9:30"},
    {"Brisbane", "AEST-10"},
    {"Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Sydney / Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Johannesburg", "SAST-2"},
    {"Nairobi", "EAT-3"},
    {"Sao Paulo", "BRT3"},
    {"Buenos Aires", "ART3"}};
const size_t pos_timezone_count =
    sizeof(pos_timezones) / sizeof(*pos_timezones);
static bool number(const char **p, unsigned min, unsigned max) {
  if (!isdigit((unsigned char)**p))
    return false;
  unsigned n = 0, count = 0;
  while (isdigit((unsigned char)**p)) {
    if (++count > 3)
      return false;
    n = n * 10 + *(*p)++ - '0';
  }
  return n >= min && n <= max;
}
static bool name(const char **p) {
  unsigned n = 0;
  if (**p == '<') {
    ++*p;
    while (isalnum((unsigned char)**p) || **p == '+' || **p == '-') {
      ++n;
      ++*p;
    }
    if (**p != '>')
      return false;
    ++*p;
  } else
    while (isalpha((unsigned char)**p)) {
      ++n;
      ++*p;
    }
  return n >= 3;
}
static bool offset(const char **p, unsigned max) {
  if (**p == '+' || **p == '-')
    ++*p;
  if (!number(p, 0, max))
    return false;
  for (unsigned i = 0; i < 2 && **p == ':'; ++i) {
    ++*p;
    if (!number(p, 0, 59))
      return false;
  }
  return true;
}
static bool rule(const char **p) {
  if (**p == 'M') {
    ++*p;
    if (!number(p, 1, 12) || **p != '.')
      return false;
    ++*p;
    if (!number(p, 1, 5) || **p != '.')
      return false;
    ++*p;
    if (!number(p, 0, 6))
      return false;
  } else {
    bool julian = **p == 'J';
    if (julian)
      ++*p;
    if (!number(p, julian ? 1 : 0, 365))
      return false;
  }
  if (**p == '/') {
    ++*p;
    if (!offset(p, 167))
      return false;
  }
  return true;
}
bool pos_timezone_valid(const char *s) {
  if (!s || !*s || strlen(s) > 63)
    return false;
  const char *p = s;
  if (!name(&p) || !offset(&p, 24))
    return false;
  if (!*p)
    return true;
  if (!name(&p))
    return false;
  if (*p != ',' && !offset(&p, 24))
    return false;
  if (*p++ != ',' || !rule(&p) || *p++ != ',' || !rule(&p) || *p)
    return false;
  return true;
}
bool pos_time_parse_utc(const char *text, int64_t *epoch) {
  if (!text || !epoch || strlen(text) != 16 || text[4] != '-' ||
      text[7] != '-' || text[10] != ' ' || text[13] != ':')
    return false;
  for (unsigned i = 0; i < 16; ++i)
    if (i != 4 && i != 7 && i != 10 && i != 13 &&
        !isdigit((unsigned char)text[i]))
      return false;
  unsigned y, m, d, h, n;
  if (sscanf(text, "%4u-%2u-%2u %2u:%2u", &y, &m, &d, &h, &n) != 5 ||
      y < 2020 || y > 2099 || m < 1 || m > 12 || h > 23 || n > 59)
    return false;
  static const unsigned md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned days = md[m - 1] + (m == 2 && y % 4 == 0);
  if (d < 1 || d > days)
    return false;
  int64_t total = 0;
  for (unsigned i = 1970; i < y; ++i)
    total += 365 + (i % 4 == 0 && (i % 100 != 0 || i % 400 == 0));
  for (unsigned i = 1; i < m; ++i)
    total += md[i - 1] + (i == 2 && y % 4 == 0);
  *epoch = ((total + d - 1) * 24 + h) * 3600 + n * 60;
  return true;
}
