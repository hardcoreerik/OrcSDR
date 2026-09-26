#include "freq_keypad.hpp"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>

namespace orcsdr::freq_keypad {
namespace {

constexpr uint16_t kCyan = 0x05FF;
constexpr uint16_t kGreen = 0x6FE0;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kKeyFill = 0x2945;
constexpr uint16_t kCardFill = 0x0841;

constexpr int kCardX = 220, kCardY = 140, kCardW = 840, kCardH = 572;
constexpr int kFieldX = 250, kFieldY = 192, kFieldW = 780, kFieldH = 70;
constexpr int kKeyX = 250, kKeyY = 276, kKeyW = 250, kKeyH = 78;
constexpr int kKeyPitchX = 265, kKeyPitchY = 88;
constexpr int kActionY = 630, kActionH = 72, kActionW = 380;
constexpr int kCancelX = 250, kTuneX = 650;
constexpr char kKeys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '\b'};

char g_hint[24]{};
char g_unit[8]{};

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void label(const char* value, int x, int y, uint16_t color, uint16_t background,
           uint8_t size) {
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, background);
  M5.Display.setTextSize(size);
  M5.Display.drawString(value, x, y);
}

void key(int x, int y, int w, int h, const char* value, uint16_t edge, uint16_t fill,
         uint16_t color, uint8_t size) {
  M5.Display.fillRoundRect(x, y, w, h, 10, fill);
  M5.Display.drawRoundRect(x, y, w, h, 10, edge);
  label(value, x + w / 2, y + h / 2, color, fill, size);
}

void draw_field(const char* entry) {
  M5.Display.fillRoundRect(kFieldX, kFieldY, kFieldW, kFieldH, 10, TFT_NAVY);
  char field[40];
  if (entry[0]) snprintf(field, sizeof(field), "%s %s", entry, g_unit);
  else snprintf(field, sizeof(field), "%s", g_hint);
  label(field, kFieldX + kFieldW / 2, kFieldY + kFieldH / 2,
        entry[0] ? TFT_WHITE : kMuted, TFT_NAVY, entry[0] ? 5 : 4);
}

int key_x(int index) { return kKeyX + (index % 3) * kKeyPitchX; }
int key_y(int index) { return kKeyY + (index / 3) * kKeyPitchY; }

}  // namespace

void draw(int clear_top, uint16_t background, const char* title, const char* hint,
          const char* unit, const char* entry) {
  snprintf(g_hint, sizeof(g_hint), "%s", hint ? hint : "");
  snprintf(g_unit, sizeof(g_unit), "%s", unit ? unit : "");
  M5.Display.setFont(nullptr);
  M5.Display.clearScrollRect();
  M5.Display.fillRect(0, clear_top, 1280, 720 - clear_top, background);
  M5.Display.fillRoundRect(kCardX, kCardY, kCardW, kCardH, 16, kCardFill);
  M5.Display.drawRoundRect(kCardX, kCardY, kCardW, kCardH, 16, kCyan);
  label(title ? title : "ENTER FREQUENCY", kCardX + kCardW / 2, 166, kCyan, kCardFill, 3);
  draw_field(entry ? entry : "");
  for (int i = 0; i < 12; ++i) {
    const char value[2] = {kKeys[i] == '\b' ? '<' : kKeys[i], '\0'};
    key(key_x(i), key_y(i), kKeyW, kKeyH, value, kCyan, kKeyFill, TFT_WHITE, 5);
  }
  key(kCancelX, kActionY, kActionW, kActionH, "CANCEL", TFT_RED, kCardFill, TFT_RED, 4);
  key(kTuneX, kActionY, kActionW, kActionH, "TUNE", kGreen, kGreen, TFT_BLACK, 4);
}

Result handle_touch(int32_t x, int32_t y, char* entry, size_t entry_size) {
  if (entry == nullptr || entry_size < 2) return Result::none;
  if (hit(x, y, kCancelX, kActionY, kActionW, kActionH)) return Result::cancelled;
  if (hit(x, y, kTuneX, kActionY, kActionW, kActionH)) return Result::submitted;
  for (int i = 0; i < 12; ++i) {
    if (!hit(x, y, key_x(i), key_y(i), kKeyW, kKeyH)) continue;
    const size_t n = strlen(entry);
    if (kKeys[i] == '\b') {
      if (n) entry[n - 1] = '\0';
    } else if (n + 1 < entry_size && (kKeys[i] != '.' || strchr(entry, '.') == nullptr)) {
      entry[n] = kKeys[i];
      entry[n + 1] = '\0';
    }
    draw_field(entry);
    return Result::changed;
  }
  return Result::none;
}

bool self_check() {
  // Keys, actions and the card stay on the panel and never overlap.
  return key_x(2) + kKeyW <= kCardX + kCardW && key_y(11) + kKeyH < kActionY &&
         kTuneX + kActionW <= kCardX + kCardW && kActionY + kActionH <= kCardY + kCardH &&
         kCardY + kCardH <= 720 && kCancelX + kActionW < kTuneX &&
         kFieldY + kFieldH < kKeyY;
}

}  // namespace orcsdr::freq_keypad
