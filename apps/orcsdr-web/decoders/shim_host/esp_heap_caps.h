// Host and WebAssembly builds: the allocator calls the sources make, on plain malloc.
#pragma once
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
static inline void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
static inline void* heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
static inline void* heap_caps_aligned_alloc(size_t a, size_t n, uint32_t) { return aligned_alloc(a, (n + a - 1) / a * a); }
static inline void heap_caps_free(void* p) { free(p); }
static inline size_t heap_caps_get_allocated_size(void* p) { return p ? malloc_usable_size(p) : 0; }
static inline size_t heap_caps_get_free_size(uint32_t) { return 1u << 28; }
