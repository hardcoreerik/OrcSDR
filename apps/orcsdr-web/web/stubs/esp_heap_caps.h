#pragma once
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
static inline void* heap_caps_aligned_alloc(size_t a, size_t n, int) { return aligned_alloc(a, (n + a - 1) / a * a); }
static inline size_t heap_caps_get_free_size(int) { return 1u << 24; }
static inline size_t heap_caps_get_largest_free_block(int) { return 1u << 24; }
static inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
static inline void heap_caps_free(void* p) { free(p); }
static inline void* heap_caps_calloc(size_t n, size_t size, int) { return calloc(n, size); }
static inline void* heap_caps_realloc(void* p, size_t n, int) { return realloc(p, n); }
