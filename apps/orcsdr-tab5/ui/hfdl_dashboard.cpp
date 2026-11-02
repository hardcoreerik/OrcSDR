#include "hfdl_dashboard.hpp"

#include <M5Unified.h>
#include <cstdio>

namespace orcsdr::hfdl_dashboard {
namespace {

hfdl_ui::Model* g_model = nullptr;
hfdl_ui::Controller* g_controller = nullptr;
bool g_active = false;

constexpr uint16_t kCyan = 0x05FF;
constexpr uint16_t kGreen = 0x6FE0;
constexpr uint16_t kPanel = 0x0021;
constexpr int kListX = 20, kListY = 120, kListW = 1240, kRowH = 52;

void draw_list() {
  if (!g_model) return;
  const auto& snap = g_model->snapshot();
  M5.Display.fillRect(kListX, kListY, kListW, kRowH * 8, TFT_BLACK);
  for (size_t i = 0; i < snap.message_count; ++i) {
    const auto& m = snap.messages[i];
    const int y = kListY + static_cast<int>(i) * kRowH;
    M5.Display.fillRoundRect(kListX, y, kListW, kRowH - 4, 6, kPanel);
    M5.Display.drawRoundRect(kListX, y, kListW, kRowH - 4, 6, kCyan);
    char line[64];
    snprintf(line, sizeof(line), "%u  %s  %.3f MHz  %s",
             m.seq, m.label, m.frequency_hz / 1e6, m.preview);
    M5.Display.setTextColor(kGreen, kPanel);
    M5.Display.setTextSize(2);
    M5.Display.drawString(line, kListX + 12, y + 16);
  }
}

}  // namespace

void install(hfdl_ui::Model* model, hfdl_ui::Controller* controller) {
  g_model = model;
  g_controller = controller;
}

void enter() {
  g_active = true;
  M5.Display.fillScreen(TFT_BLACK);
  draw();
}

void leave() { g_active = false; }

void draw() {
  if (!g_active) return;
  M5.Display.setFont(nullptr);
  M5.Display.setTextColor(kCyan, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.drawString("HFDL DECODER", 20, 40);
  draw_list();
}

void update(uint32_t /*now_ms*/) {
  if (!g_active || !g_model) return;
  g_model->service();
  draw_list();
}

bool active() { return g_active; }

}  // namespace orcsdr::hfdl_dashboard
