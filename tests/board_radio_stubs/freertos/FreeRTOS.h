#pragma once
typedef void* SemaphoreHandle_t;
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
