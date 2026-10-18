#pragma once
#include <assert.h>
#include <stdlib.h>
typedef unsigned *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1,sizeof(unsigned)); }
static inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) { (void)timeout; assert(s && !*s); *s=1; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { assert(s && *s); *s=0; return 1; }
