#pragma once
typedef int BaseType_t;
typedef void* TaskHandle_t;
#define pdPASS 1
#define pdTRUE 1
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdMS_TO_TICKS(ms) (ms)
