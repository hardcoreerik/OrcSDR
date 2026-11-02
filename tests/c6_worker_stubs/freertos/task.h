#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*);
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}
