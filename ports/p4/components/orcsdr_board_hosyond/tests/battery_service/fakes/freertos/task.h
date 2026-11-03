#pragma once

#include "freertos/FreeRTOS.h"

typedef struct fake_task_control *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack_depth,
                       void *argument, UBaseType_t priority, TaskHandle_t *out_handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment);
