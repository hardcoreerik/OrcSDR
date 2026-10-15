#pragma once
// WAVESHARE 4B FM: five-tab IQ-only preview; no WFM audio or RDS claimed.
#include "home_v3_ui.hpp"
namespace orcsdr::waveshare4b::fm {
bool create(QueueHandle_t radio_commands,void(*return_home)()); // LVGL display locked
void open();    // UI task only; does not change radio frequency itself
void close();   // UI task only; retain cached canvas, no USB interference
bool active();
void update(const home_v3::Snapshot&,const uint8_t* fft256,bool fresh);
}
