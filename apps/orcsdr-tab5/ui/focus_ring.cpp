#include "focus_ring.hpp"

#include <M5Unified.h>
#include <esp_attr.h>

#include <algorithm>

namespace orcsdr::focus_ring {
namespace {

constexpr int kThickness = 3;
constexpr int kGap = 3;                 // ring sits this far outside the control
constexpr uint16_t kColor = 0xFFE0;     // yellow: readable on every dashboard background
constexpr size_t kPixelBudget = 24000;  // four strips of a full-width control fit comfortably

struct Strip {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  size_t offset = 0;
};

EXT_RAM_BSS_ATTR lgfx::rgb565_t g_pixels[kPixelBudget];
Strip g_strips[4];
bool g_visible = false;

Strip clip(int x, int y, int w, int h) {
  Strip strip;
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(1280, x + w);
  const int y1 = std::min(720, y + h);
  if (x1 > x0 && y1 > y0) {
    strip.x = x0;
    strip.y = y0;
    strip.w = x1 - x0;
    strip.h = y1 - y0;
  }
  return strip;
}

}  // namespace

void hide() {
  if (!g_visible) return;
  for (const Strip& strip : g_strips)
    if (strip.w > 0 && strip.h > 0)
      M5.Display.pushImage(strip.x, strip.y, strip.w, strip.h, g_pixels + strip.offset);
  g_visible = false;
}

void forget() { g_visible = false; }

bool visible() { return g_visible; }

void show(const focus_nav::Rect& rect) {
  hide();
  const int left = rect.x - kGap - kThickness;
  const int top = rect.y - kGap - kThickness;
  const int outer_w = rect.w + 2 * (kGap + kThickness);
  const int outer_h = rect.h + 2 * (kGap + kThickness);
  g_strips[0] = clip(left, top, outer_w, kThickness);                                   // top
  g_strips[1] = clip(left, top + outer_h - kThickness, outer_w, kThickness);            // bottom
  g_strips[2] = clip(left, top + kThickness, kThickness, outer_h - 2 * kThickness);     // left
  g_strips[3] = clip(left + outer_w - kThickness, top + kThickness, kThickness,
                     outer_h - 2 * kThickness);                                         // right
  size_t used = 0;
  for (Strip& strip : g_strips) {
    const size_t pixels = static_cast<size_t>(strip.w) * strip.h;
    if (used + pixels > kPixelBudget) {  // too big to save safely: skip that strip
      strip = Strip{};
      continue;
    }
    strip.offset = used;
    used += pixels;
  }
  M5.Display.startWrite();
  for (const Strip& strip : g_strips) {
    if (strip.w <= 0 || strip.h <= 0) continue;
    M5.Display.readRect(strip.x, strip.y, strip.w, strip.h, g_pixels + strip.offset);
    M5.Display.fillRect(strip.x, strip.y, strip.w, strip.h, kColor);
  }
  M5.Display.endWrite();
  g_visible = true;
}

}  // namespace orcsdr::focus_ring
