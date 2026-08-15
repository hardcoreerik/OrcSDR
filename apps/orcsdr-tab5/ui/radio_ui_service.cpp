#include "radio_ui_service.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstdio>

namespace orcsdr::radio_ui {
namespace {
constexpr uint16_t kGrid = 0x2104;
}

uint16_t waterfall_color(float level) {
  level = std::clamp(level, 0.0f, 1.0f);
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  if (level < 0.25f) {
    blue = static_cast<uint8_t>(40 + level * 720.0f);
  } else if (level < 0.5f) {
    const float ramp = (level - 0.25f) * 4.0f;
    green = static_cast<uint8_t>(ramp * 220.0f);
    blue = 220;
  } else if (level < 0.75f) {
    const float ramp = (level - 0.5f) * 4.0f;
    red = static_cast<uint8_t>(ramp * 255.0f);
    green = 220;
    blue = static_cast<uint8_t>((1.0f - ramp) * 220.0f);
  } else {
    const float ramp = (level - 0.75f) * 4.0f;
    red = 255;
    green = static_cast<uint8_t>(220 + ramp * 35.0f);
    blue = static_cast<uint8_t>(ramp * 255.0f);
  }
  return M5.Display.color565(red, green, blue);
}

void draw_grid(const ScopeGeometry& geometry) {
  for (int line = 1; line < 4; ++line) {
    const int y = geometry.y + line * geometry.height / 4;
    M5.Display.drawFastHLine(geometry.x + 1, y, geometry.width - 2, kGrid);
  }
  M5.Display.drawFastVLine(geometry.x + geometry.width / 2, geometry.y + 1,
                           geometry.height - 2, TFT_GREEN);
}

void draw_axis(const ScopeGeometry& geometry, const ScopeState& state) {
  const double center = state.frequency_hz / 1000000.0;
  const double half_span = static_cast<double>(state.span_hz) / 2000000.0;
  char label[32];
  M5.Display.fillRect(geometry.x - 24, geometry.y + geometry.height + 1,
                      geometry.width + 24, 19, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  for (int marker = 0; marker <= 4; ++marker) {
    const double mark = center - half_span + marker * (half_span / 2.0);
    if (state.cb_channels) {
      snprintf(label, sizeof(label), "CH %u",
               static_cast<unsigned>(state.cb_marker_channels[marker] + 1));
    } else if (state.frequency_hz >= 1000000u) {
      snprintf(label, sizeof(label), marker == 4 ? "%.3f MHz" : "%.3f", mark);
    } else {
      snprintf(label, sizeof(label), marker == 4 ? "%.1f kHz" : "%.1f", mark * 1000.0);
    }
    M5.Display.setTextColor(marker == 2 ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString(label, geometry.x + marker * geometry.width / 4,
                          geometry.y + geometry.height + 11);
  }
}

void draw_filter_edges(const ScopeGeometry& geometry, const ScopeState& state) {
  if (state.span_hz == 0) return;
  const int half_width = std::clamp(
      static_cast<int>((static_cast<uint64_t>(state.filter_bandwidth_hz) * geometry.width) /
                       (2u * state.span_hz)),
      3, geometry.width / 2 - 2);
  const int center = geometry.x + geometry.width / 2;
  for (int offset = -1; offset <= 1; ++offset) {
    M5.Display.drawFastVLine(center - half_width + offset, geometry.y + 1,
                             geometry.height - 2, TFT_YELLOW);
    M5.Display.drawFastVLine(center + half_width + offset, geometry.y + 1,
                             geometry.height - 2, TFT_YELLOW);
  }
}

}  // namespace orcsdr::radio_ui
