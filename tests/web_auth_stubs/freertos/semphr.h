#pragma once
#include "FreeRTOS.h"
extern "C" SemaphoreHandle_t xSemaphoreCreateMutex();
extern "C" int xSemaphoreTake(SemaphoreHandle_t, unsigned);
extern "C" int xSemaphoreGive(SemaphoreHandle_t);
