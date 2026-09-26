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

// Full-width layout below the 72 px header: large keys and size-4 letters so
// the keyboard is usable with a fingertip on the 1280x720 panel.
constexpr int kLeft = 20, kWidth = 1240;
constexpr int kTitleY = 100;
constexpr int kFieldY = 124, kFieldH = 64;
constexpr int kKeyY = 202, kKeyH = 74, kKeyPitch = 82, kKeyGap = 8;
constexpr int kFnY = 534, kFnH = 66;
constexpr int kActionY = 614, kActionH = 80, kActionW = 320;

struct Box { int x, y, w, h; };
constexpr Box kShift{kLeft, kFnY, 190, kFnH};
constexpr Box kSymbols{kLeft + 205, kFnY, 210, kFnH};
constexpr Box kSpace{kLeft + 430, kFnY, 370, kFnH};
constexpr Box kBack{kLeft + 815, kFnY, 200, kFnH};
constexpr Box kReveal{kLeft + 1030, kFnY, 210, kFnH};
constexpr Box kCancel{kLeft, kActionY, kActionW, kActionH};
constexpr Box kAccept{kLeft + kWidth - kActionW, kActionY, kActionW, kActionH};

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

bool hit(int x, int y, const Box& b) { return hit(x, y, b.x, b.y, b.w, b.h); }

void button(const char* label, int x, int y, int w, int h, uint16_t color,
            uint8_t size = 3) {
  M5.Display.fillRoundRect(x, y, w, h, 8, 0x1082);
  M5.Display.drawRoundRect(x, y, w, h, 8, color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color == TFT_DARKGREY ? TFT_WHITE : color, 0x1082);
  M5.Display.setTextSize(size);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

void button(const char* label, const Box& b, uint16_t color) {
  button(label, b.x, b.y, b.w, b.h, color);
}

// Every row uses the ten-key width; shorter rows are centred like a real
// keyboard, and the 12-key symbol row shrinks to fit.
Box key_box(int row_index, int col, int count) {
  const int ten = (kWidth - 9 * kKeyGap) / 10;
  const int width = std::min(ten, (kWidth - (count - 1) * kKeyGap) / count);
  const int span = count * width + (count - 1) * kKeyGap;
  return {kLeft + (kWidth - span) / 2 + col * (width + kKeyGap),
          kKeyY + row_index * kKeyPitch, width, kKeyH};
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
  M5.Display.fillRect(0, 72, 1280, 648, bg);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(cyan, bg);
  M5.Display.setTextSize(3);
  M5.Display.drawString(g.title, kLeft, kTitleY);
  char shown[64]{};
  if (g.masked && !g.reveal) memset(shown, '*', strlen(g.value));
  else strlcpy(shown, g.value, sizeof(shown));
  button(shown[0] ? shown : " ", kLeft, kFieldY, kWidth, kFieldH, TFT_NAVY, 3);
  for (int r = 0; r < 4; ++r) {
    const char* keys = row(r);
    const int count = static_cast<int>(strlen(keys));
    for (int col = 0; col < count; ++col) {
      char label[2] = {keys[col], '\0'};
      const Box b = key_box(r, col, count);
      button(label, b.x, b.y, b.w, b.h, TFT_DARKGREY, 4);
    }
  }
  button(g.shift ? "SHIFT ON" : "SHIFT", kShift, TFT_DARKCYAN);
  button(g.symbols ? "ABC" : "SYMBOLS", kSymbols, TFT_DARKCYAN);
  button("SPACE", kSpace, TFT_DARKGREY);
  button("BACK", kBack, TFT_DARKGREY);
  if (g.masked) button(g.reveal ? "HIDE" : "SHOW", kReveal, TFT_NAVY);
  button("CANCEL", kCancel, TFT_MAROON);
  button(g.accept, kAccept, TFT_DARKGREEN);
}

Result handle_touch(int x, int y) {
  if (!g.open) return Result::none;
  if (hit(x, y, kCancel)) { g.open = false; return Result::cancelled; }
  if (hit(x, y, kAccept)) { g.open = false; return Result::accepted; }
  if (hit(x, y, kShift)) { g.shift = !g.shift; g.symbols = false; draw(); return Result::changed; }
  if (hit(x, y, kSymbols)) { g.symbols = !g.symbols; draw(); return Result::changed; }
  size_t length = strlen(g.value);
  if (hit(x, y, kSpace)) {
    if (length < g.maximum) g.value[length++] = ' ', g.value[length] = '\0';
    draw(); return Result::changed;
  }
  if (hit(x, y, kBack)) {
    if (length) g.value[length - 1] = '\0';
    draw(); return Result::changed;
  }
  if (g.masked && hit(x, y, kReveal)) {
    g.reveal = !g.reveal; draw(); return Result::changed;
  }
  for (int r = 0; r < 4; ++r) {
    const char* keys = row(r);
    const int count = static_cast<int>(strlen(keys));
    for (int col = 0; col < count; ++col) {
      if (!hit(x, y, key_box(r, col, count))) continue;
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
  // Layout: the symbol row fits the panel and rows do not overlap the function row.
  const Box last = key_box(2, 11, 12);
  return ok && !active() && last.x + last.w <= kLeft + kWidth &&
         key_box(3, 0, 7).y + kKeyH < kFnY && kFnY + kFnH < kActionY &&
         kActionY + kActionH <= 720 && kReveal.x + kReveal.w <= kLeft + kWidth;
}

}  // namespace orcsdr::text_editor
