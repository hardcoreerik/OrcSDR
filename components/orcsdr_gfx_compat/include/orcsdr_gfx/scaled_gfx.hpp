#pragma once
// M5GFX-compatible drawing front end that scales the Tab5's 1280x720 coordinate space onto a
// smaller display. The Tab5 UI code (apps/orcsdr-tab5/ui) is written entirely as M5GFX calls
// with absolute 1280x720 coordinates. These classes accept those same calls and arguments,
// scale every coordinate, and draw with M5GFX's own engine (an lgfx::LGFX_Sprite), so shapes,
// text rendering, arcs and rounded rectangles come out of the same code that draws the Tab5.
//
//   ScaledDisplay  stands in for M5.Display (the whole screen)
//   ScaledCanvas   stands in for M5Canvas   (an off-screen sprite)
//
// Text: Font0 is drawn at the nearest whole-pixel multiple (each font pixel a c x c block);
// other fonts map to the closest smaller bundled font. textWidth() and width()/height() report
// DESIGN units so the UI's own layout arithmetic keeps working.
#define LGFX_USE_V1
#include "lgfx/v1/platforms/device.hpp"
#include "lgfx/v1/platforms/common.hpp"
#include "lgfx/v1/LGFXBase.hpp"
#include "lgfx/v1/LGFX_Sprite.hpp"
// The real LovyanGFX headers already export what the Tab5 sources use unqualified (TFT_*, the
// text datums, the global `fonts` namespace); no extra using-directives here, they cause ambiguity.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace orcsdr::gfx {

constexpr int kDesignW = 1280, kDesignH = 720;

class ScaledGfx {
 public:
  virtual ~ScaledGfx() = default;

  // --- state -------------------------------------------------------------------------------
  void setTextSize(float size) { text_size_ = size; style_dirty_ = true; }
  void setFont(const lgfx::IFont* font) { font_ = font; style_dirty_ = true; }
  // Colours keep their C++ type, which is how M5GFX tells RGB565 (uint16_t, int) from RGB888 (uint32_t).
  template <typename T> void setTextColor(const T& fg) { fg_ = lgfx::convert_to_rgb888(fg); bg_ = fg_; style_dirty_ = true; }
  template <typename T1, typename T2> void setTextColor(const T1& fg, const T2& bg) {
    fg_ = lgfx::convert_to_rgb888(fg);
    bg_ = lgfx::convert_to_rgb888(bg);
    style_dirty_ = true;
  }
  void setTextDatum(lgfx::textdatum_t datum) { datum_ = datum; style_dirty_ = true; }
  void setTextPadding(uint32_t padding) { (void)padding; }
  void setTextWrap(bool, bool = false) {}
  void setSwapBytes(bool) {}
  void setRotation(int) {}
  int getRotation() const { return 0; }

  // --- geometry in design units --------------------------------------------------------------
  int width() const { return dw_; }
  int height() const { return dh_; }
  int32_t textWidth(const char* text);
  int32_t fontHeight();

  // --- drawing (design coordinates) ----------------------------------------------------------
  template <typename T> void fillScreen(const T& color);
  template <typename T> void fillRect(int x, int y, int w, int h, const T& color);
  template <typename T> void drawRect(int x, int y, int w, int h, const T& color);
  template <typename T> void fillRoundRect(int x, int y, int w, int h, int r, const T& color);
  template <typename T> void drawRoundRect(int x, int y, int w, int h, int r, const T& color);
  template <typename T> void drawFastHLine(int x, int y, int w, const T& color);
  template <typename T> void drawFastVLine(int x, int y, int h, const T& color);
  template <typename T> void drawLine(int x0, int y0, int x1, int y1, const T& color);
  template <typename T> void drawPixel(int x, int y, const T& color);
  template <typename T> void drawCircle(int x, int y, int r, const T& color);
  template <typename T> void fillCircle(int x, int y, int r, const T& color);
  template <typename T> void drawArc(int x, int y, int r0, int r1, float angle0, float angle1, const T& color);
  template <typename T> void fillArc(int x, int y, int r0, int r1, float angle0, float angle1, const T& color);
  template <typename T> void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const T& color);
  int32_t drawString(const char* text, int x, int y);
  void setClipRect(int x, int y, int w, int h);
  void clearClipRect();
  template <typename T = int> void setScrollRect(int x, int y, int w, int h, const T& color = T());
  void clearScrollRect();
  void scroll(int dx, int dy);
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) const { return lgfx::color565(r, g, b); }
  uint32_t color888(uint8_t r, uint8_t g, uint8_t b) const { return lgfx::color888(r, g, b); }
  void startWrite() {}
  void endWrite() {}

  // Images are resampled (nearest neighbour) from design size to target size.
  template <typename T> void pushImage(int x, int y, int w, int h, const T* data);
  // Reads the area back at design size (nearest neighbour), as big-endian RGB565 like M5GFX's uint16_t overload.
  void readRect(int x, int y, int w, int h, uint16_t* out);

  // --- target access for the compat layer ----------------------------------------------------
  lgfx::LGFX_Sprite* target() { return t_; }
  int X(int x) const { return static_cast<int>(std::lround(x * sx_)); }
  int Y(int y) const { return static_cast<int>(std::lround(y * sy_)); }
  float scale_x() const { return sx_; }
  float scale_y() const { return sy_; }
  int cell_for(float design_size) const;

 protected:
  ScaledGfx() = default;
  void bind(lgfx::LGFX_Sprite* target, int design_w, int design_h, int target_w, int target_h) {
    t_ = target;
    dw_ = design_w;
    dh_ = design_h;
    tw_ = target_w;
    th_ = target_h;
    sx_ = static_cast<float>(target_w) / design_w;
    sy_ = static_cast<float>(target_h) / design_h;
  }
  // Called with every touched rectangle in TARGET pixels (the display records dirty rows).
  virtual void dirty(int x0, int y0, int x1, int y1) { (void)x0; (void)y0; (void)x1; (void)y1; }
  void apply_style();
  void mark_rect(int x, int y, int w, int h);  // design rect -> dirty()

  lgfx::LGFX_Sprite* t_ = nullptr;
  int dw_ = kDesignW, dh_ = kDesignH, tw_ = kDesignW, th_ = kDesignH;
  float sx_ = 1.0f, sy_ = 1.0f;
  float text_size_ = 1.0f;
  const lgfx::IFont* font_ = nullptr;
  uint32_t fg_ = 0xFFFFFF, bg_ = 0xFFFFFF;
  lgfx::textdatum_t datum_ = lgfx::textdatum_t::top_left;
  bool style_dirty_ = true;
  // scroll rect state (scaled) and fractional-row accumulator
  int scroll_x_ = 0, scroll_y_ = 0, scroll_w_ = 0, scroll_h_ = 0;
  float scroll_accum_y_ = 0.0f, scroll_accum_x_ = 0.0f;
  bool has_scroll_ = false;
};

// A persistent PSRAM scratch buffer that grows on demand (UI task only).
void* scaled_gfx_scratch(size_t bytes);

template <typename T>
void ScaledGfx::pushImage(int x, int y, int w, int h, const T* data) {
  const int px = X(x), py = Y(y), pw = X(x + w) - px, ph = Y(y + h) - py;
  if (!data || w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return;  // thin strips can round to nothing: skipped
  T* tmp = static_cast<T*>(scaled_gfx_scratch(sizeof(T) * static_cast<size_t>(pw) * ph));
  if (!tmp) return;
  for (int j = 0; j < ph; ++j) {
    const T* src = data + static_cast<size_t>(static_cast<int64_t>(j) * h / ph) * w;
    T* dst = tmp + static_cast<size_t>(j) * pw;
    for (int i = 0; i < pw; ++i) dst[i] = src[static_cast<int>(static_cast<int64_t>(i) * w / pw)];
  }
  t_->pushImage(px, py, pw, ph, tmp);
  dirty(px, py, px + pw, py + ph);
}


// ---- colour-taking draws: templates so each caller's colour type reaches M5GFX unchanged -------
template <typename T> void ScaledGfx::fillScreen(const T& color) {
  t_->fillScreen(color);
  dirty(0, 0, tw_, th_);
}
template <typename T> void ScaledGfx::fillRect(int x, int y, int w, int h, const T& color) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  t_->fillRect(px, py, pw, ph, color);
  dirty(px, py, px + pw, py + ph);
}
template <typename T> void ScaledGfx::drawRect(int x, int y, int w, int h, const T& color) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  t_->drawRect(px, py, pw, ph, color);
  dirty(px, py, px + pw, py + ph);
}
template <typename T> void ScaledGfx::fillRoundRect(int x, int y, int w, int h, int r, const T& color) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  t_->fillRoundRect(px, py, pw, ph, static_cast<int>(std::lround(r * 0.5f * (sx_ + sy_))), color);
  dirty(px, py, px + pw, py + ph);
}
template <typename T> void ScaledGfx::drawRoundRect(int x, int y, int w, int h, int r, const T& color) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  t_->drawRoundRect(px, py, pw, ph, static_cast<int>(std::lround(r * 0.5f * (sx_ + sy_))), color);
  dirty(px, py, px + pw, py + ph);
}
template <typename T> void ScaledGfx::drawFastHLine(int x, int y, int w, const T& color) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px);
  t_->drawFastHLine(px, py, pw, color);
  dirty(px, py, px + pw, py + 1);
}
template <typename T> void ScaledGfx::drawFastVLine(int x, int y, int h, const T& color) {
  const int px = X(x), py = Y(y), ph = std::max(1, Y(y + h) - py);
  t_->drawFastVLine(px, py, ph, color);
  dirty(px, py, px + 1, py + ph);
}
template <typename T> void ScaledGfx::drawLine(int x0, int y0, int x1, int y1, const T& color) {
  t_->drawLine(X(x0), Y(y0), X(x1), Y(y1), color);
  dirty(std::min(X(x0), X(x1)), std::min(Y(y0), Y(y1)), std::max(X(x0), X(x1)) + 1, std::max(Y(y0), Y(y1)) + 1);
}
template <typename T> void ScaledGfx::drawPixel(int x, int y, const T& color) {
  t_->drawPixel(X(x), Y(y), color);
  dirty(X(x), Y(y), X(x) + 1, Y(y) + 1);
}
template <typename T> void ScaledGfx::drawCircle(int x, int y, int r, const T& color) {
  const int pr = std::max(1, static_cast<int>(std::lround(r * 0.5f * (sx_ + sy_))));
  t_->drawCircle(X(x), Y(y), pr, color);
  dirty(X(x) - pr, Y(y) - pr, X(x) + pr + 1, Y(y) + pr + 1);
}
template <typename T> void ScaledGfx::fillCircle(int x, int y, int r, const T& color) {
  const int pr = std::max(1, static_cast<int>(std::lround(r * 0.5f * (sx_ + sy_))));
  t_->fillCircle(X(x), Y(y), pr, color);
  dirty(X(x) - pr, Y(y) - pr, X(x) + pr + 1, Y(y) + pr + 1);
}
template <typename T> void ScaledGfx::drawArc(int x, int y, int r0, int r1, float a0, float a1, const T& color) {
  const float k = 0.5f * (sx_ + sy_);
  const int p0 = static_cast<int>(std::lround(r0 * k)), p1 = static_cast<int>(std::lround(r1 * k));
  t_->drawArc(X(x), Y(y), p0, p1, a0, a1, color);
  const int outer = std::max(p0, p1);
  dirty(X(x) - outer, Y(y) - outer, X(x) + outer + 1, Y(y) + outer + 1);
}
template <typename T> void ScaledGfx::fillArc(int x, int y, int r0, int r1, float a0, float a1, const T& color) {
  const float k = 0.5f * (sx_ + sy_);
  const int p0 = static_cast<int>(std::lround(r0 * k)), p1 = static_cast<int>(std::lround(r1 * k));
  t_->fillArc(X(x), Y(y), p0, p1, a0, a1, color);
  const int outer = std::max(p0, p1);
  dirty(X(x) - outer, Y(y) - outer, X(x) + outer + 1, Y(y) + outer + 1);
}
template <typename T> void ScaledGfx::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, const T& color) {
  t_->fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), color);
  dirty(std::min({X(x0), X(x1), X(x2)}), std::min({Y(y0), Y(y1), Y(y2)}), std::max({X(x0), X(x1), X(x2)}) + 1,
        std::max({Y(y0), Y(y1), Y(y2)}) + 1);
}
template <typename T> void ScaledGfx::setScrollRect(int x, int y, int w, int h, const T& color) {
  const int px = X(x), py = Y(y);
  scroll_x_ = px;
  scroll_y_ = py;
  scroll_w_ = std::max(1, X(x + w) - px);
  scroll_h_ = std::max(1, Y(y + h) - py);
  has_scroll_ = true;
  t_->setScrollRect(scroll_x_, scroll_y_, scroll_w_, scroll_h_, color);
}

// The whole screen. Draws into an RGB565 sprite in PSRAM and remembers which rows changed.
class ScaledDisplay : public ScaledGfx {
 public:
  ScaledDisplay() = default;
  // Allocates the backing sprite. design: the Tab5 grid (1280x720); target: the real panel.
  bool begin(int target_w, int target_h, int design_w = kDesignW, int design_h = kDesignH);
  bool ready() const { return ready_; }

  // Dirty-row bookkeeping for the presenter. Returns false when nothing changed.
  bool take_dirty(int* y0, int* y1);
  // Used by canvases that pushed themselves into the display (target pixels).
  void mark_dirty_target(int x0, int y0, int x1, int y1);
  void mark_all_dirty() { dirty_y0_ = 0; dirty_y1_ = th_; }
  const uint16_t* buffer() const { return static_cast<const uint16_t*>(sprite_.getBuffer()); }
  size_t stride_pixels() const { return static_cast<size_t>(tw_); }
  int target_width() const { return tw_; }
  int target_height() const { return th_; }
  lgfx::LGFX_Sprite& sprite() { return sprite_; }

 protected:
  void dirty(int x0, int y0, int x1, int y1) override;

 private:
  lgfx::LGFX_Sprite sprite_;
  bool ready_ = false;
  int dirty_y0_ = 0, dirty_y1_ = 0;  // empty when y0 >= y1
};

// An off-screen sprite, created and drawn in design units.
class ScaledCanvas : public ScaledGfx {
 public:
  ScaledCanvas() = default;
  explicit ScaledCanvas(ScaledDisplay* parent) : parent_(parent) {}
  ~ScaledCanvas() override { deleteSprite(); }
  ScaledCanvas(const ScaledCanvas&) = delete;
  ScaledCanvas& operator=(const ScaledCanvas&) = delete;

  void setPsram(bool) { psram_ = true; }  // always PSRAM on this target
  void setColorDepth(int bits) { depth_ = bits; }
  bool createSprite(int w, int h);
  void deleteSprite();
  bool created() const { return created_; }
  template <typename T> void fillSprite(const T& color) { if (created_) sprite_.fillSprite(color); }
  void pushSprite(int x, int y);
  template <typename T> void pushSprite(int x, int y, const T& transparent);
  void pushSprite(ScaledDisplay* dst, int x, int y);
  void* frameBuffer(uint8_t) { return sprite_.getBuffer(); }

 private:
  ScaledDisplay* parent_ = nullptr;
  lgfx::LGFX_Sprite sprite_;
  bool created_ = false, psram_ = true;
  int depth_ = 16;
};

template <typename T>
void ScaledCanvas::pushSprite(int x, int y, const T& transparent) {
  if (!created_ || !parent_) return;
  const int px = parent_->X(x), py = parent_->Y(y);
  sprite_.pushSprite(parent_->target(), px, py, transparent);
  parent_->mark_dirty_target(px, py, px + tw_, py + th_);
}

}  // namespace orcsdr::gfx
