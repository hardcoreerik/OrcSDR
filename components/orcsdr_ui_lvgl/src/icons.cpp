#include "orcsdr_ui/icons.hpp"
#include <algorithm>

namespace orcsdr::ui::icons {
using orcsdr::dashboards::Id;

void wifi(Painter& g, int x, int y, uint16_t color) {
  g.drawArc(x, y, 22, 19, 215, 325, color);
  g.drawArc(x, y, 14, 11, 215, 325, color);
  g.fillCircle(x, y + 7, 3, color);
}

void usb(Painter& g, int x, int y, uint16_t color) {
  g.drawFastVLine(x, y - 17, 31, color);
  g.drawLine(x, y - 17, x - 5, y - 10, color);
  g.drawLine(x, y - 17, x + 5, y - 10, color);
  g.drawLine(x, y + 2, x - 9, y - 6, color);
  g.fillCircle(x - 10, y - 7, 3, color);
  g.drawLine(x, y + 9, x + 9, y + 2, color);
  g.drawRect(x + 7, y - 1, 5, 5, color);
}

void menu(Painter& g, Id id, int x, int y, uint16_t color) {
  if (id == Id::home) {
    g.fillTriangle(x - 14, y, x, y - 13, x + 14, y, color);
    g.drawRect(x - 10, y, 20, 15, color);
  } else if (id == Id::fm || id == Id::am) {
    g.drawRoundRect(x - 15, y - 10, 30, 22, 4, color);
    g.drawCircle(x + 7, y + 1, 5, color);
    g.drawLine(x - 10, y - 14, x + 10, y - 20, color);
  } else if (id == Id::p25) {
    g.drawRect(x - 9, y - 17, 18, 34, color);
    g.drawFastVLine(x - 3, y - 11, 12, color);
    g.drawFastVLine(x + 3, y - 11, 12, color);
  } else if (id == Id::adsb) {
    g.fillTriangle(x - 18, y + 9, x + 18, y - 12, x + 3, y + 14, color);
    g.drawFastVLine(x - 2, y - 7, 27, color);
  } else if (id == Id::shortwave || id == Id::airband || id == Id::marine || id == Id::satellite) {
    g.drawCircle(x, y, 17, color);
    g.drawFastHLine(x - 17, y, 34, color);
    g.drawFastVLine(x, y - 17, 34, color);
  } else if (id == Id::lora) {
    g.fillCircle(x, y - 2, 3, color);
    g.drawFastVLine(x, y + 1, 16, color);
    g.drawArc(x, y - 2, 11, 9, 300, 60, color);
    g.drawArc(x, y - 2, 11, 9, 120, 240, color);
    g.drawArc(x, y - 2, 18, 16, 300, 60, color);
    g.drawArc(x, y - 2, 18, 16, 120, 240, color);
  } else if (id == Id::rf_lab) {
    g.drawRoundRect(x - 18, y - 14, 36, 28, 4, color);
    g.drawLine(x - 14, y + 3, x - 8, y + 3, color);
    g.drawLine(x - 8, y + 3, x - 3, y - 8, color);
    g.drawLine(x - 3, y - 8, x + 3, y + 9, color);
    g.drawLine(x + 3, y + 9, x + 8, y - 3, color);
    g.drawLine(x + 8, y - 3, x + 14, y - 3, color);
  } else if (id == Id::settings) {
    g.drawCircle(x, y, 15, color);
    g.fillCircle(x, y, 5, color);
  } else if (id == Id::ft8) {
    // Eight FSK tones: the FT8 symbol alphabet.
    for (int tone = 0; tone < 8; ++tone) g.drawFastVLine(x - 14 + tone * 4, y + 12 - tone * 3, 4 + tone * 3, color);
    g.drawFastHLine(x - 16, y + 14, 34, color);
  } else if (id == Id::wifi_analysis) {
    g.drawCircle(x, y + 7, 3, color);
    g.drawArc(x, y + 7, 12, 12, 210, 330, color);
    g.drawArc(x, y + 7, 20, 20, 210, 330, color);
  } else {
    g.drawLine(x - 13, y - 13, x + 13, y + 13, color);
    g.drawLine(x + 13, y - 13, x - 13, y + 13, color);
    g.drawCircle(x, y, 5, color);
  }
}

void speaker(Painter& g, int cx, int cy, uint16_t color, bool enabled) {
  g.fillTriangle(cx - 13, cy - 7, cx - 5, cy - 7, cx - 5, cy + 7, color);
  g.fillTriangle(cx - 5, cy - 7, cx + 5, cy - 14, cx + 5, cy + 14, color);
  if (enabled) {
    g.drawArc(cx + 4, cy, 12, 9, 300, 60, color);
    g.drawArc(cx + 4, cy, 19, 16, 300, 60, color);
  } else {
    g.drawLine(cx + 9, cy - 12, cx + 25, cy + 12, color);
    g.drawLine(cx + 25, cy - 12, cx + 9, cy + 12, color);
  }
}

void battery(Painter& g, int x, int y, int percent, uint16_t outline, uint16_t full, uint16_t empty) {
  g.drawRoundRect(x, y, 68, 32, 5, outline);
  g.fillRect(x + 68, y + 9, 6, 14, outline);
  const int pct = std::clamp(percent, 0, 100);
  const int cells = percent < 0 ? 0 : (pct + 24) / 25;
  for (int i = 0; i < 4; ++i) g.fillRect(x + 7 + i * 14, y + 6, 11, 20, i < cells ? full : empty);
}

void home_house(Painter& g, int home_x, int home_w, uint16_t color, uint16_t panel) {
  const int cx = home_x + home_w / 2;
  g.fillTriangle(cx, 17, home_x + 9, 38, home_x + home_w - 9, 38, color);
  g.fillRect(home_x + 17, 35, 24, 21, color);
  g.fillRect(cx - 4, 43, 8, 13, panel);
}

void gear(Painter& g, int cx, int cy, uint16_t color) {
  g.drawCircle(cx, cy, 13, color);
  g.drawCircle(cx, cy, 5, color);
  g.drawLine(cx - 21, cy, cx - 13, cy, color);
  g.drawLine(cx + 13, cy, cx + 21, cy, color);
  g.drawLine(cx, cy - 21, cx, cy - 13, color);
  g.drawLine(cx, cy + 13, cx, cy + 21, color);
}

void all_dashboards_grid(Painter& g, int x, int y, uint16_t color) {
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) g.fillRect(x + col * 11, y + row * 9, 6, 6, color);
}

}  // namespace orcsdr::ui::icons
