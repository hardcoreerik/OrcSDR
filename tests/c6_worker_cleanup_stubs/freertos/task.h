#pragma once
#include <freertos/FreeRTOS.h>
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
