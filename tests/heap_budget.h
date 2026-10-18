/* POS allocations only; libc/crypto internals are outside this budget. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
typedef union {
  max_align_t align;
  struct {
    size_t size;
    uint32_t magic;
  } h;
} allocation_t;
static size_t used, peak, largest, limit = SIZE_MAX, single = SIZE_MAX,
                                   attempts, fail_at, denied;
void *__wrap_malloc(size_t n) {
  ++attempts;
  if ((fail_at && attempts == fail_at) || n > single || n > limit - used ||
      n > SIZE_MAX - sizeof(allocation_t)) {
    ++denied;
    return NULL;
  }
  allocation_t *a = __real_malloc(sizeof(*a) + n);
  if (!a)
    return NULL;
  a->h.size = n;
  a->h.magic = 0x504f5348;
  used += n;
  if (used > peak)
    peak = used;
  if (n > largest)
    largest = n;
  return a + 1;
}
void __wrap_free(void *p) {
  if (!p)
    return;
  allocation_t *a = (allocation_t *)p - 1;
  assert(a->h.magic == 0x504f5348);
  used -= a->h.size;
  a->h.magic = 0;
  __real_free(a);
}
void *__wrap_calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size)
    return NULL;
  void *p = __wrap_malloc(n * size);
  if (p)
    memset(p, 0, n * size);
  return p;
}
void *__wrap_realloc(void *p, size_t n) {
  if (!p)
    return __wrap_malloc(n);
  if (!n) {
    __wrap_free(p);
    return NULL;
  }
  allocation_t *a = (allocation_t *)p - 1;
  assert(a->h.magic == 0x504f5348);
  ++attempts;
  if ((fail_at && attempts == fail_at) || n > single ||
      n > limit - used + a->h.size) {
    ++denied;
    return NULL;
  }
  size_t old = a->h.size;
  a = __real_realloc(a, sizeof(*a) + n);
  if (!a)
    return NULL;
  a->h.size = n;
  used = used - old + n;
  if (used > peak)
    peak = used;
  if (n > largest)
    largest = n;
  return a + 1;
}
