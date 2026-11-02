#pragma once
// HFDL dashboard M5GFX renderer (decoder portion).
// Draws the LIVE message list from the Snapshot. Thin for now.

#include <cstdint>

#include "hfdl_dashboard_model.hpp"
#include "hfdl_dashboard_ui.hpp"

namespace orcsdr::hfdl_dashboard {

void install(hfdl_ui::Model* model, hfdl_ui::Controller* controller);
void enter();
void leave();
void draw();
void update(uint32_t now_ms);
bool active();

}  // namespace orcsdr::hfdl_dashboard
