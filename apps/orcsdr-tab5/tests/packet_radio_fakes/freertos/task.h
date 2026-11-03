#pragma once
#include "FreeRTOS.h"
struct FakeTask;
using TaskHandle_t = FakeTask*;
using TaskFunction_t = void (*)(void*);
BaseType_t xTaskCreate(TaskFunction_t function, const char* name, unsigned stack,
                       void* argument, unsigned priority, TaskHandle_t* handle);
void vTaskDelete(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t wait);
void xTaskNotifyGive(TaskHandle_t task);
