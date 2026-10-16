#pragma once
// Recording stand-in for M5Unified / M5GFX used by the ACARS dashboard host tests. It performs no drawing:
// every primitive is recorded with its bounding box and the clip rectangle in force, so tests can prove
// where the real renderer would have painted (for example, never inside the shared status bar).
// M5Canvas models an off-screen sprite: primitives drawn into it reach the "screen" only when pushSprite()
// is called, translated to the push position, exactly like the real display would receive them.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using textdatum_t = int;
constexpr int top_left = 0, middle_left = 1, middle_center = 2, middle_right = 3;
constexpr uint16_t TFT_BLACK = 0, TFT_WHITE = 0xffff, TFT_LIGHTGREY = 0xd69a, TFT_DARKGREY = 0x7bef;

namespace fonts {
struct Font { int advance; int height; };
inline Font DejaVu18{10, 22}, DejaVu24{13, 28}, DejaVu40{22, 46}, FreeMono12pt7b{14, 24};
}  // namespace fonts

struct Prim {
  enum Kind : uint8_t { fill, outline, text, line, circle, triangle } kind;
  int x, y, w, h;               // bounding box before clipping
  bool clipped;                 // a clip rectangle was active
  int cx, cy, cw, ch;           // that clip rectangle
  bool from_text;
  uint16_t color;
  std::string str;              // drawString content
};

namespace host_display {
inline std::vector<Prim> prims;          // what reached the screen
inline bool clip_on = false;
inline int clip_x, clip_y, clip_w, clip_h;
inline const fonts::Font* font = nullptr;
inline int datum = middle_left;
inline uint16_t text_color = 0xffff;
inline int direct_ops = 0;               // primitives drawn straight onto the display (not via a sprite)
inline int pushes = 0;                   // sprite pushes to the display
inline bool fail_sprite_alloc = false;   // make createSprite() fail, to exercise the direct-draw fallback
inline void reset() {
  prims.clear(); clip_on = false; font = nullptr; direct_ops = 0; pushes = 0;
}
// Effective painted rectangle (bounding box intersected with the clip). Returns false if nothing is painted.
inline bool effective(const Prim& p, int* x, int* y, int* w, int* h) {
  int x0 = p.x, y0 = p.y, x1 = p.x + p.w, y1 = p.y + p.h;
  if (p.clipped) {
    x0 = std::max(x0, p.cx); y0 = std::max(y0, p.cy);
    x1 = std::min(x1, p.cx + p.cw); y1 = std::min(y1, p.cy + p.ch);
  }
  if (x1 <= x0 || y1 <= y0) return false;
  *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
  return true;
}
}  // namespace host_display

namespace lgfx {
class LovyanGFX {
 public:
  std::vector<Prim>* sink = &host_display::prims;
  bool is_display = true;

  void setFont(const fonts::Font* f) { host_display::font = f; }
  void setTextDatum(int d) { host_display::datum = d; }
  void setTextSize(float) {}
  void setTextColor(uint16_t c) { host_display::text_color = c; }
  void setClipRect(int x, int y, int w, int h) {
    host_display::clip_on = true;
    host_display::clip_x = x; host_display::clip_y = y; host_display::clip_w = w; host_display::clip_h = h;
  }
  void clearClipRect() { host_display::clip_on = false; }
  int textWidth(const char* s) {
    const fonts::Font* f = host_display::font ? host_display::font : &fonts::DejaVu18;
    return static_cast<int>(std::strlen(s)) * f->advance;
  }
  void fillRect(int x, int y, int w, int h, uint16_t c) { add(Prim::fill, x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t c) { add(Prim::fill, x, y, w, h, c); }
  void drawRoundRect(int x, int y, int w, int h, int, uint16_t) { add(Prim::outline, x, y, w, h); }
  void drawFastHLine(int x, int y, int w, uint16_t) { add(Prim::line, x, y, w, 1); }
  void drawFastVLine(int x, int y, int h, uint16_t) { add(Prim::line, x, y, 1, h); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t) {
    add(Prim::line, std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1);
  }
  void drawCircle(int x, int y, int r, uint16_t) { add(Prim::circle, x - r, y - r, 2 * r + 1, 2 * r + 1); }
  void fillCircle(int x, int y, int r, uint16_t) { add(Prim::circle, x - r, y - r, 2 * r + 1, 2 * r + 1); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t) {
    const int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2});
    const int miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    add(Prim::triangle, minx, miny, maxx - minx + 1, maxy - miny + 1);
  }
  void fillScreen(uint16_t) {}
  void drawString(const char* s, int x, int y) {
    const fonts::Font* f = host_display::font ? host_display::font : &fonts::DejaVu18;
    const int w = static_cast<int>(std::strlen(s)) * f->advance;
    int x0 = x;
    if (host_display::datum == middle_center) x0 = x - w / 2;
    else if (host_display::datum == middle_right) x0 = x - w;
    add(Prim::text, x0, y - f->height / 2, w, f->height, host_display::text_color, s);
  }

 protected:
  void add(Prim::Kind k, int x, int y, int w, int h, uint16_t color = 0, const char* text = nullptr) {
    sink->push_back({k, x, y, w, h, host_display::clip_on, host_display::clip_x, host_display::clip_y,
                     host_display::clip_w, host_display::clip_h, k == Prim::text, color,
                     text ? std::string(text) : std::string()});
    if (is_display) ++host_display::direct_ops;
  }
};
}  // namespace lgfx

class M5Canvas : public lgfx::LovyanGFX {
 public:
  explicit M5Canvas(lgfx::LovyanGFX* parent) : parent_(parent) { sink = &local_; is_display = false; }
  void setPsram(bool) {}
  void setColorDepth(int) {}
  void* getBuffer() { return nullptr; }   // no pixels are modelled: the dashboard then pushes whole frames
  void* createSprite(int w, int h) {
    if (host_display::fail_sprite_alloc) return nullptr;
    w_ = w; h_ = h;
    return this;
  }
  void deleteSprite() { w_ = h_ = 0; local_.clear(); }
  void fillSprite(uint16_t c) { local_.clear(); add(Prim::fill, 0, 0, w_, h_, c); }
  // Moves everything drawn into the sprite onto the screen, translated to (x, y).
  void pushSprite(int x, int y) {
    for (Prim p : local_) {
      p.x += x; p.y += y;
      if (p.clipped) { p.cx += x; p.cy += y; }
      parent_->sink->push_back(p);
    }
    ++host_display::pushes;
    local_.clear();
  }

 private:
  lgfx::LovyanGFX* parent_;
  std::vector<Prim> local_;
  int w_ = 0, h_ = 0;
};

struct HostPower {
  int level = 77;
  int getBatteryLevel() const { return level; }
};
inline struct M5Stub { lgfx::LovyanGFX Display; HostPower Power; } M5;
inline uint32_t host_millis = 1000;
inline uint32_t millis() { return host_millis; }
