#pragma once
// The OrcSDR icons, drawn from primitives exactly as the Tab5 does
// (apps/orcsdr-tab5/ui/home_dashboard.cpp and dashboard_audio_control.cpp). Coordinates are the
// Tab5's design space; the Painter maps them onto the target surface.
#include <cstdint>
#include "dashboard_registry.hpp"
#include "orcsdr_ui/painter.hpp"

namespace orcsdr::ui::icons {
void wifi(Painter& g, int x, int y, uint16_t color);
void usb(Painter& g, int x, int y, uint16_t color);
void menu(Painter& g, orcsdr::dashboards::Id id, int x, int y, uint16_t color);
void speaker(Painter& g, int cx, int cy, uint16_t color, bool enabled);
void battery(Painter& g, int x, int y, int percent, uint16_t outline, uint16_t full, uint16_t empty);
void home_house(Painter& g, int home_x, int home_w, uint16_t color, uint16_t panel);
void gear(Painter& g, int cx, int cy, uint16_t color);
void all_dashboards_grid(Painter& g, int x, int y, uint16_t color);
}  // namespace orcsdr::ui::icons
