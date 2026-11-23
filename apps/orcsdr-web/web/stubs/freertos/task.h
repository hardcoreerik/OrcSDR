#pragma once
#include "FreeRTOS.h"
// A task never runs in the page: whatever needs a background worker is driven by the controller instead.
#include <string.h>
// Dead Air's SD journal worker never starts here (no SD card in a page), which its screen reports as it does for a missing card.
static inline BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*)(void*), const char* name, uint32_t, void*, int, TaskHandle_t* h, int, int) {
  if (name && !strcmp(name, "dead_air_sd")) return 0;
  if (h) *h = (void*)1;
  return pdPASS;
}
static inline void vTaskDeleteWithCaps(TaskHandle_t) {}
static inline void vTaskDelay(int) {}
