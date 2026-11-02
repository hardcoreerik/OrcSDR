#pragma once
#include <cstdint>
using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = unsigned;
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
constexpr BaseType_t pdPASS = 1;
constexpr TickType_t pdMS_TO_TICKS(unsigned n) { return n; }
