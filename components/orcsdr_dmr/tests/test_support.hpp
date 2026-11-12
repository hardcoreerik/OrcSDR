#pragma once
#include <cstdio>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (false)
