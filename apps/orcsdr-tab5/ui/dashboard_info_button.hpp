#pragma once
// The standard (i) help button of every dashboard: one look, one place in the header (right end of the status-bar area, left of the
// divider at x865, level with the Home and Settings buttons). Rounded square like the header buttons: panel fill and cyan edge, green
// when its card is open. Header-only and templated on the display so host tests can draw it onto the recording stub.
#include <M5Unified.h>

namespace orcsdr::info_button {
constexpr int kX = 804, kY = 16, kW = 48, kH = 48;
constexpr bool hit(int x, int y) { return x >= kX && x < kX + kW && y >= kY && y < kY + kH; }
template <class Display>
void draw(Display& g, bool open) {
  constexpr uint16_t kPanel = 0x0841, kCyan = 0x2e7f, kGreen = 0x6fe8, kSelected = 0x1264;
  g.fillRoundRect(kX, kY, kW, kH, 8, open ? kSelected : kPanel);
  g.drawRoundRect(kX, kY, kW, kH, 8, open ? kGreen : kCyan);
  g.setFont(nullptr);
  g.setTextSize(3);
  g.setTextDatum(middle_left);
  g.setTextColor(open ? kGreen : kCyan);
  g.drawString("i", kX + (kW - g.textWidth("i")) / 2, kY + kH / 2);
}
}  // namespace orcsdr::info_button
