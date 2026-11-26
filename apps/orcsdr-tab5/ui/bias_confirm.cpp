#include "bias_confirm.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

namespace orcsdr::bias_confirm {
namespace {
constexpr int kBadgeX = 1016, kBadgeY = 72, kBadgeW = 240, kBadgeH = 32;     // the red rectangle (see draw_global_bias_warning)
constexpr int kX = 300, kY = 240, kW = 680, kH = 240;                         // the popup
constexpr int kBtnY = kY + 170, kBtnH = 50;
constexpr int kConfirmX = kX + 20, kConfirmW = 320;
constexpr int kCancelW = 200, kCancelX = kX + kW - 20 - kCancelW;
constexpr uint16_t kCyan = 0x2e7f, kPanel = 0x0841;

uint16_t* g_under = nullptr;     // the pixels the popup covers (PSRAM, only while open)
bool g_active = false;

bool inside(int32_t x, int32_t y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void button(int x, int w, const char* label, bool danger) {
  M5.Display.fillRoundRect(x, kBtnY, w, kBtnH, 8, danger ? TFT_MAROON : kPanel);
  M5.Display.drawRoundRect(x, kBtnY, w, kBtnH, 8, danger ? TFT_RED : kCyan);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, danger ? TFT_MAROON : kPanel);
  M5.Display.drawString(label, x + w / 2, kBtnY + kBtnH / 2);
}

void draw() {
  M5.Display.startWrite();
  M5.Display.fillRoundRect(kX, kY, kW, kH, 12, TFT_BLACK);
  M5.Display.drawRoundRect(kX, kY, kW, kH, 12, TFT_RED);
  M5.Display.drawRoundRect(kX + 1, kY + 1, kW - 2, kH - 2, 11, TFT_RED);
  M5.Display.setFont(nullptr);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.drawString("ANTENNA PORT POWER (BIAS TEE)", kX + 20, kY + 30);
  M5.Display.setTextDatum(middle_right);
  M5.Display.drawString("NOW ON", kX + kW - 20, kY + 30);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("5 V is on the antenna connector for an LNA.", kX + 20, kY + 80);
  M5.Display.drawString("It stays on until you turn it off.", kX + 20, kY + 112);
  M5.Display.drawString("A restart turns it off.", kX + 20, kY + 144);
  button(kConfirmX, kConfirmW, "CONFIRM: TURN OFF", true);
  button(kCancelX, kCancelW, "CANCEL", false);
  M5.Display.endWrite();
}
}  // namespace

bool rect_hit(int32_t x, int32_t y) { return inside(x, y, kBadgeX, kBadgeY, kBadgeW, kBadgeH); }

bool active() { return g_active; }

void open() {
  if (g_active) return;
  if (g_under == nullptr)
    g_under = static_cast<uint16_t*>(heap_caps_malloc(static_cast<size_t>(kW) * kH * sizeof(uint16_t),
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_under != nullptr) M5.Display.readRect(kX, kY, kW, kH, g_under);   // without it the popup still works; the screen repaints itself
  g_active = true;
  draw();
}

void close() {
  if (!g_active) return;
  g_active = false;
  if (g_under != nullptr) {
    M5.Display.pushImage(kX, kY, kW, kH, g_under);
    heap_caps_free(g_under);
    g_under = nullptr;
  }
}

Result touch(int32_t x, int32_t y) {
  if (!g_active) return Result::none;
  if (inside(x, y, kConfirmX, kBtnY, kConfirmW, kBtnH)) {
    close();
    return Result::turn_off;
  }
  if (inside(x, y, kCancelX, kBtnY, kCancelW, kBtnH) || !inside(x, y, kX, kY, kW, kH)) {
    close();
    return Result::closed;
  }
  return Result::none;      // inside the popup but on nothing: stay open
}

}  // namespace orcsdr::bias_confirm
