#pragma once
// Native M5GFX HFDL dashboard. No tuner ownership; consumes verified model events.
#include <cstdint>
#include "hfdl_dashboard_model.hpp"
#include "hfdl_dashboard_ui.hpp"

namespace orcsdr::hfdl_dashboard {
using HeaderHook=void (*)();
void install(hfdl_ui::Model* model,hfdl_ui::Controller* controller,
             HeaderHook header_controls=nullptr);
void set_overlay_probe(bool (*overlay_open)());
void enter();
void leave();
void draw();               // structural transition only
void update(uint32_t now_ms); // bounded dirty region redraws
hfdl_ui::Action touch(int32_t x,int32_t y);
bool active();
const hfdl_ui::layout::View& viewport();
}  // namespace orcsdr::hfdl_dashboard
