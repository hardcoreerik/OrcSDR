#pragma once
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace orcsdr::waveshare4b::home_v3 {
enum class Command:uint8_t {Down,Up,Step,FM,WX,Air,Ham};
struct Snapshot {uint32_t requested_hz,applied_hz,effective_sps,drops,step_hz,uptime_sec;int receiver_state;};
bool create(QueueHandle_t commands); // call under BSP LVGL lock
void update(const Snapshot& status,const uint8_t* bins256,bool fresh); // LVGL task only
}
