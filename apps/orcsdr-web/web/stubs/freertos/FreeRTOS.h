// Web build: there are no tasks or critical sections (the page is one thread). Just enough for the sources that name them.
#pragma once
#include <stdint.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define tskIDLE_PRIORITY 0
typedef void* TaskHandle_t;
typedef int BaseType_t;
