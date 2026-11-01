#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline void* heap_caps_malloc(size_t size,int){return std::malloc(size);}
inline void heap_caps_free(void* p){std::free(p);}
