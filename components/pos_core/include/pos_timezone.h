#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {const char *name,*rule;} pos_timezone_t;
extern const pos_timezone_t pos_timezones[];
extern const size_t pos_timezone_count;
bool pos_timezone_valid(const char *rule);
bool pos_time_parse_utc(const char *text,int64_t *epoch);
