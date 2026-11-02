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
uint64_t g_generation=0;
constexpr Rect kLegacyViewport{0, 72, 1280, 648};
Rect g_viewport = kLegacyViewport;
Layout g_layout = make_layout(kLegacyViewport);

bool hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

bool hit(int x, int y, Rect b) { return hit(x, y, b.x, b.y, b.w, b.h); }

void button(const char* label, Rect r, uint16_t color,
            uint8_t size = 3) {
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 8, 0x1082);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 8, color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color == TFT_DARKGREY ? TFT_WHITE : color, 0x1082);
  if(!g_layout.legacy && size==2 && r.h>=24 && label && strlen(label)*18<=size_t(std::max(0,r.w-16))) size=3;
  M5.Display.setTextSize(size);
  M5.Display.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
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

void draw_field() {
  char shown[64]{};
  if (g.masked && !g.reveal) memset(shown, '*', strlen(g.value));
  else strlcpy(shown, g.value, sizeof(shown));
  if (!g_layout.legacy) {
    M5.Display.setTextSize(2);
    const int char_width = std::max<int32_t>(1, M5.Display.textWidth("M"));
    const int capacity = std::max(1, (g_layout.field.w - 24) / char_width);
    const int length = static_cast<int>(strlen(shown));
    if (length > capacity) {
      const int tail_capacity = std::max(1, capacity - 3);
      const int tail_start = length - tail_capacity;
      char tail[64]{};
      tail[0] = '.'; tail[1] = '.'; tail[2] = '.';
      strlcpy(tail + 3, shown + tail_start, sizeof(tail) - 3);
      strlcpy(shown, tail, sizeof(shown));
    }
  }
  button(shown[0] ? shown : " ", g_layout.field, TFT_NAVY,
         g_layout.legacy ? 3 : 2);
}

void draw_key_buttons() {
  M5.Display.fillRect(g_layout.keyboard.x, g_layout.keyboard.y,
                      g_layout.keyboard.w, g_layout.keyboard.h, 0x0841);
  for (int r = 0; r < 4; ++r) {
    const char* keys = row(r);
    const int count = static_cast<int>(strlen(keys));
    for (int col = 0; col < count; ++col) {
      char label[2] = {keys[col], '\0'};
      button(label, g_layout.keys[r][col], TFT_DARKGREY,
             g_layout.legacy ? 4 : (g_layout.keyboard.w < 800 ? 3 : 2));
    }
  }
}

void draw_reveal_button() {
  if (g.masked)
    button(g.reveal ? "HIDE" : "SHOW", g_layout.reveal, TFT_NAVY,
           g_layout.legacy ? 3 : 2);
}

void draw_function_buttons() {
  const bool narrow = !g_layout.legacy && g_layout.viewport.w < 800;
  const uint8_t size = g_layout.legacy ? 3 : 2;
  button((narrow || (!g_layout.legacy && g_layout.viewport.w < 900))
             ? (g.shift ? "SHIFT*" : "SHIFT")
             : (g.shift ? "SHIFT ON" : "SHIFT"),
         g_layout.shift, TFT_DARKCYAN, size);
  button(g.symbols ? "ABC" : (narrow ? "SYM" : "SYMBOLS"),
         g_layout.symbols, TFT_DARKCYAN, size);
  button("SPACE", g_layout.space, TFT_DARKGREY, size);
  button("BACK", g_layout.back, TFT_DARKGREY, size);
  draw_reveal_button();
  button("CANCEL", g_layout.cancel, TFT_MAROON, size);
  button(g.accept, g_layout.accept, TFT_DARKGREEN, size);
}

void draw_keyboard() {
  draw_key_buttons();
  draw_function_buttons();
}
}  // namespace

void begin(const char* title, const char* initial, size_t maximum_length,
           bool masked, const char* accept_label) {
  ++g_generation;
  g = {};
  strlcpy(g.title, title ? title : "EDIT TEXT", sizeof(g.title));
  strlcpy(g.value, initial ? initial : "", sizeof(g.value));
  strlcpy(g.accept, accept_label ? accept_label : "SAVE", sizeof(g.accept));
  g.maximum = std::min(maximum_length, sizeof(g.value) - 1);
  g.value[g.maximum] = '\0';
  g.masked = masked;
  g.open = true;
  g_layout = make_layout(g_viewport, false);
}

bool set_viewport(Rect owned_area) {
  const Layout next = make_layout(owned_area, g.symbols);
  if (!next.valid) return false;
  g_viewport = owned_area;
  g_layout = next;
  return true;
}

const Layout& layout() { return g_layout; }
bool active() { return g.open; }
uint64_t generation() { return g_generation; }
const char* value() { return g.value; }
void close() { g.open = false; }

void draw() {
  if (!g.open) return;
  constexpr uint16_t bg = 0x0841, cyan = 0x04ff;
  M5.Display.setFont(nullptr);
  M5.Display.fillRect(g_layout.viewport.x, g_layout.viewport.y,
                      g_layout.viewport.w, g_layout.viewport.h, bg);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(cyan, bg);
  M5.Display.setTextSize(g_layout.legacy || strlen(g.title)*18<=size_t(g_layout.title.w) ? 3 : 2);
  M5.Display.drawString(g.title, g_layout.title.x,
                        g_layout.legacy ? g_layout.title.y
                                        : g_layout.title.y + g_layout.title.h / 2);
  draw_field();
  draw_keyboard();
}

Result handle_touch(int x, int y) {
  if (!g.open) return Result::none;
  if (hit(x, y, g_layout.cancel)) { g.open = false; return Result::cancelled; }
  if (hit(x, y, g_layout.accept)) { g.open = false; return Result::accepted; }
  if (hit(x, y, g_layout.shift)) {
    g.shift = !g.shift;
    g.symbols = false;
    g_layout = make_layout(g_viewport, false);
    draw_keyboard();
    return Result::changed;
  }
  if (hit(x, y, g_layout.symbols)) {
    g.symbols = !g.symbols;
    g_layout = make_layout(g_viewport, g.symbols);
    draw_keyboard();
    return Result::changed;
  }
  size_t length = strlen(g.value);
  if (hit(x, y, g_layout.space)) {
    if (length < g.maximum) g.value[length++] = ' ', g.value[length] = '\0';
    draw_field(); return Result::changed;
  }
  if (hit(x, y, g_layout.back)) {
    if (length) g.value[length - 1] = '\0';
    draw_field(); return Result::changed;
  }
  if (g.masked && hit(x, y, g_layout.reveal)) {
    g.reveal = !g.reveal;
    draw_field();
    draw_reveal_button();
    return Result::changed;
  }
  for (int r = 0; r < 4; ++r) {
    const char* keys = row(r);
    const int count = static_cast<int>(strlen(keys));
    for (int col = 0; col < count; ++col) {
      if (!hit(x, y, g_layout.keys[r][col])) continue;
      if (length < g.maximum) g.value[length] = keys[col], g.value[length + 1] = '\0';
      if (g.shift && !g.symbols) {
        g.shift = false;
        draw_field();
        draw_keyboard();
      } else {
        draw_field();
      }
      return Result::changed;
    }
  }
  return Result::none;
}

keyboard_input::EditAction handle_key(const keyboard_input::Key& key) {
  using keyboard_input::EditAction;
  if (!g.open) return EditAction::none;
  const EditAction action =
      keyboard_input::apply_edit(g.value, sizeof(g.value), g.maximum, key);
  if (action == EditAction::edited) {
    draw_field();
  } else if (action == EditAction::toggle_reveal) {
    if (!g.masked) return EditAction::none;
    g.reveal = !g.reveal;
    draw_field();
    draw_reveal_button();
  }
  return action;
}

bool masked() { return g.open && g.masked; }
void accept_point(int* x, int* y) {
  if (x) *x = g_layout.accept.x + g_layout.accept.w / 2;
  if (y) *y = g_layout.accept.y + g_layout.accept.h / 2;
}
void cancel_point(int* x, int* y) {
  if (x) *x = g_layout.cancel.x + g_layout.cancel.w / 2;
  if (y) *y = g_layout.cancel.y + g_layout.cancel.h / 2;
}

bool self_check() {
  const Layout legacy = make_layout(kLegacyViewport, true);
  const Layout portrait = make_layout({72, 72, 424, 960});
  const Layout landscape = make_layout({72, 72, 1088, 360});
  if (!legacy.valid || !legacy.legacy || legacy.field.x != 20 ||
      legacy.keys[2][10].w <= 0 || legacy.keys[2][10].y != 366 ||
      !portrait.valid || !landscape.valid) return false;
  for (const Layout* candidate : {&portrait, &landscape}) {
    for (int r = 0; r < 4; ++r)
      for (std::size_t c = 0; c < candidate->key_counts[r]; ++c)
        if (!inside(candidate->viewport, candidate->keys[r][c])) return false;
    if (!inside(candidate->viewport, candidate->field) ||
        !inside(candidate->viewport, candidate->accept)) return false;
  }
  return true;
}

}  // namespace orcsdr::text_editor
