#pragma once

#include "freertos/task.h"

void fake_freertos_reset(void);
void fake_freertos_set_tick(TickType_t tick);
void fake_freertos_advance_tick(TickType_t ticks);
void fake_freertos_fail_next_task_create(void);
unsigned fake_freertos_task_create_count(void);
uint32_t fake_freertos_last_stack_depth(void);
UBaseType_t fake_freertos_last_priority(void);
