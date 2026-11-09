// Minimal LovyanGFX platform layer for WASM (replaces platforms/framebuffer/common.cpp).
#include <lgfx/v1/platforms/common.hpp>
#include <emscripten.h>
namespace lgfx { inline namespace v1 {
unsigned long millis(void) { return (unsigned long)emscripten_get_now(); }
unsigned long micros(void) { return (unsigned long)(emscripten_get_now() * 1000.0); }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}
void gpio_hi(uint32_t) {}
void gpio_lo(uint32_t) {}
bool gpio_in(uint32_t) { return false; }
void pinMode(int_fast16_t, pin_mode_t) {}
void lgfxPinMode(int_fast16_t, pin_mode_t) {}
}}
