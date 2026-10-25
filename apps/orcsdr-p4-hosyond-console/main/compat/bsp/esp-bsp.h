#pragma once
// Compatibility shim: the recovered Waveshare 4B screenshot service was written against
// the 4B BSP's display lock. On the Hosyond target the same contract is provided by
// lvgl_port.hpp, so the recovered source is compiled without modification.
#include "lvgl_port.hpp"
static inline bool bsp_display_lock(uint32_t timeout_ms) { return orcsdr_lvgl_lock(timeout_ms); }
static inline void bsp_display_unlock(void) { orcsdr_lvgl_unlock(); }
