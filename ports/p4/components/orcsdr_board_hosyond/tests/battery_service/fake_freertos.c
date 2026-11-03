#include "fake_freertos.h"

#include <string.h>

struct fake_task_control { int created; };
static struct fake_task_control s_task;
static TickType_t s_tick;
static unsigned s_create_count;
static uint32_t s_last_stack_depth;
static UBaseType_t s_last_priority;
static BaseType_t s_fail_next_create;

void fake_freertos_reset(void)
{
    memset(&s_task, 0, sizeof(s_task));
    s_tick = 0;
    s_create_count = 0;
    s_last_stack_depth = 0;
    s_last_priority = 0;
    s_fail_next_create = pdFAIL;
}

void fake_freertos_set_tick(TickType_t tick) { s_tick = tick; }
void fake_freertos_advance_tick(TickType_t ticks) { s_tick += ticks; }
void fake_freertos_fail_next_task_create(void) { s_fail_next_create = pdPASS; }
unsigned fake_freertos_task_create_count(void) { return s_create_count; }
uint32_t fake_freertos_last_stack_depth(void) { return s_last_stack_depth; }
UBaseType_t fake_freertos_last_priority(void) { return s_last_priority; }

BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack_depth,
                       void *argument, UBaseType_t priority, TaskHandle_t *out_handle)
{
    (void)name;
    (void)argument;
    ++s_create_count;
    s_last_stack_depth = stack_depth;
    s_last_priority = priority;
    if (s_fail_next_create == pdPASS) {
        s_fail_next_create = pdFAIL;
        return pdFAIL;
    }
    if (!task || !out_handle) return pdFAIL;
    s_task.created = 1;
    *out_handle = &s_task;
    return pdPASS;
}

TickType_t xTaskGetTickCount(void) { return s_tick; }

void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment)
{
    if (!previous_wake) return;
    *previous_wake += increment;
    s_tick = *previous_wake;
}
