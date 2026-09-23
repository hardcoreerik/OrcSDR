#pragma once
#include <mutex>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
inline void portENTER_CRITICAL(portMUX_TYPE* p){p->lock();}
inline void portEXIT_CRITICAL(portMUX_TYPE* p){p->unlock();}
