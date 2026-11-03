#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*entry)(void*), const char* name, unsigned stack, void* arg, unsigned priority, TaskHandle_t* handle);
void vTaskDelay(unsigned ticks);
