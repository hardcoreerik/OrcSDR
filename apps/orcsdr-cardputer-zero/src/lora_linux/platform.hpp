#pragma once
#include "fft.h"
#include <chrono>
#include <cstdlib>
#include <malloc.h>
#include <cstdint>
#include <thread>
#include <mbedtls/aes.h>
inline constexpr uint32_t MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline void* heap_caps_malloc(size_t bytes,uint32_t){return std::malloc(bytes);}
inline size_t heap_caps_get_allocated_size(void* p){return p?malloc_usable_size(p):0;}
inline void heap_caps_free(void* p){std::free(p);}
inline bool scratch_memory_eligible(const void* p){return p!=nullptr;}
inline bool esp_ptr_external_ram(const void*){return false;}
inline int64_t esp_timer_get_time(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void vTaskDelay(int){std::this_thread::yield();}
