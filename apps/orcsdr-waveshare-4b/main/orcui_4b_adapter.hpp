#pragma once
#include "home_v3_ui.hpp"
namespace orcsdr::waveshare4b::adaptive {
bool create(QueueHandle_t queue);
void update(const home_v3::Snapshot& data,const uint8_t* bins,bool fresh);
}
