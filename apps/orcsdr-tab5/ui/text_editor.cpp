#include "text_editor.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstring>

namespace orcsdr::text_editor {
namespace {
struct State {
  char title[32]{};
  char value[64]{};
  char accept[12]{};
  size_t maximum = 63;
  bool masked = false;
  bool reveal = false;
  bool shift = false;
  bool symbols = false;
  bool open = false;
} g;

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void button(const char* label, int x, int y, int w, int h, uint16_t color) {
  M5.Display.fillRoundRect(x, y, w, h, 7, 0x1082);
  M5.Display.drawRoundRect(x, y, w, h, 7, color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, 0x1082);
  M5.Display.setTextSize(2);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

const char* row(int index) {
  static constexpr const char* normal[] = {
      "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static constexpr const char* shift[] = {
      "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  static constexpr const char* symbols[] = {
      "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>?/`~", ""};
  return g.symbols ? symbols[index] : g.shift ? shift[index] : normal[index];
}
}  // namespace

void begin(const char* title, const char* initial, size_t maximum_length,
           bool masked, const char* accept_label) {
  g = {};
  strlcpy(g.title, title ? title : "EDIT TEXT", sizeof(g.title));
  strlcpy(g.value, initial ? initial : "", sizeof(g.value));
  strlcpy(g.accept, accept_label ? accept_label : "SAVE", sizeof(g.accept));
  g.maximum = std::min(maximum_length, sizeof(g.value) - 1);
  g.value[g.maximum] = '\0';
  g.masked = masked;
  g.open = true;
}

bool active() { return g.open; }
const char* value() { return g.value; }
void close() { g.open = false; }

void draw() {
  if (!g.open) return;
  constexpr uint16_t bg = 0x0841, cyan = 0x04ff;
  M5.Display.setFont(nullptr);
  M5.Display.fillRect(286, 72, 994, 648, bg);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(cyan, bg);
  M5.Display.setTextSize(3);
  M5.Display.drawString(g.title, 330, 108);
  char shown[64]{};
  if (g.masked && !g.reveal) memset(shown, '*', strlen(g.value));
  else strlcpy(shown, g.value, sizeof(shown));
  button(shown[0] ? shown : " ", 330, 140, 846, 56, TFT_NAVY);
  for (int r = 0; r < 4; ++r) {
    const char* keys = row(r);
    const int count = static_cast<int>(strlen(keys));
    if (!count) continue;
    const int gap = 6, width = (846 - (count - 1) * gap) / count;
    for (int col = 0; col < count; ++col) {
      char label[2] = {keys[col], '\0'};
      button(label, 330 + col * (width + gap), 215 + r * 62, width, 50, TFT_DARKGREY);
    }
  }
  button(g.shift ? "SHIFT ON" : "SHIFT", 330, 475, 150, 46, TFT_DARKCYAN);
  button(g.symbols ? "LETTERS" : "SYMBOLS", 492, 475, 170, 46, TFT_DARKCYAN);
  button("SPACE", 674, 475, 170, 46, TFT_DARKGREY);
  button("BACK", 856, 475, 150, 46, TFT_DARKGREY);
  if (g.masked) button(g.reveal ? "HIDE" : "SHOW", 1018, 475, 158, 46, TFT_NAVY);
  button("CANCEL", 330, 550, 250, 54, TFT_MAROON);
  button(g.accept, 926, 550, 250, 54, TFT_DARKGREEN);
}

Result handle_touch(int x, int y) {
  if (!g.open) return Result::none;
  if (hit(x, y, 330, 550, 250, 54)) { g.open = false; return Result::cancelled; }
  if (hit(x, y, 926, 550, 250, 54)) { g.open = false; return Result::accepted; }
  if (hit(x, y, 330, 475, 150, 46)) { g.shift = !g.shift; g.symbols = false; draw(); return Result::changed; }
  if (hit(x, y, 492, 475, 170, 46)) { g.symbols = !g.symbols; draw(); return Result::changed; }
  size_t length = strlen(g.value);
  if (hit(x, y, 674, 475, 170, 46)) {
    if (length < g.maximum) g.value[length++] = ' ', g.value[length] = '\0';
    draw(); return Result::changed;
  }
  if (hit(x, y, 856, 475, 150, 46)) {
    if (length) g.value[length - 1] = '\0';
    draw(); return Result::changed;
  }
  if (g.masked && hit(x, y, 1018, 475, 158, 46)) {
    g.reveal = !g.reveal; draw(); return Result::changed;
  }
  for (int r = 0; r < 4; ++r) {
    const char* keys = row(r);
    const int count = static_cast<int>(strlen(keys));
    if (!count) continue;
    const int gap = 6, width = (846 - (count - 1) * gap) / count;
    for (int col = 0; col < count; ++col) {
      if (!hit(x, y, 330 + col * (width + gap), 215 + r * 62, width, 50)) continue;
      if (length < g.maximum) g.value[length] = keys[col], g.value[length + 1] = '\0';
      if (g.shift && !g.symbols) g.shift = false;
      draw(); return Result::changed;
    }
  }
  return Result::none;
}

bool self_check() {
  begin("NAME", "abc", 4, false, "SAVE");
  const bool ok = active() && strcmp(value(), "abc") == 0;
  close();
  return ok && !active();
}

}  // namespace orcsdr::text_editor
