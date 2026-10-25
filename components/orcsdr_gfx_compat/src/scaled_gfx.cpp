#include "orcsdr_gfx/scaled_gfx.hpp"
#include <algorithm>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace orcsdr::gfx {
namespace {
constexpr char kTag[] = "GFX_COMPAT";

const void* addr(const lgfx::IFont* f) { return static_cast<const void*>(f); }

// The Tab5 fonts and what each becomes on the smaller display (see the header). DejaVu steps down
// one size; FreeMono12 becomes FreeMono9. Anything else is passed through.
const lgfx::IFont* map_font(const lgfx::IFont* font) {
  if (font == nullptr) return &lgfx::fonts::Font0;
  struct Entry { const void* from; const lgfx::IFont* to; };
  const Entry table[] = {
      {addr(&lgfx::fonts::DejaVu72), &lgfx::fonts::DejaVu40},
      {addr(&lgfx::fonts::DejaVu56), &lgfx::fonts::DejaVu40},
      {addr(&lgfx::fonts::DejaVu40), &lgfx::fonts::DejaVu24},
      {addr(&lgfx::fonts::DejaVu24), &lgfx::fonts::DejaVu18},
      {addr(&lgfx::fonts::DejaVu18), &lgfx::fonts::DejaVu12},
      {addr(&lgfx::fonts::DejaVu12), &lgfx::fonts::DejaVu9},
      {addr(&lgfx::fonts::FreeMono12pt7b), &lgfx::fonts::FreeMono9pt7b},
  };
  for (const Entry& e : table)
    if (addr(font) == e.from) return e.to;
  return font;
}

bool is_pixel_font(const lgfx::IFont* font) { return font == nullptr || addr(font) == addr(&lgfx::fonts::Font0); }

void* g_scratch = nullptr;
size_t g_scratch_bytes = 0;
}  // namespace

void* scaled_gfx_scratch(size_t bytes) {
  if (bytes > g_scratch_bytes) {
    heap_caps_free(g_scratch);
    g_scratch = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_scratch_bytes = g_scratch ? bytes : 0;
    if (!g_scratch) ESP_LOGE(kTag, "scratch allocation of %u bytes failed", unsigned(bytes));
  }
  return g_scratch;
}

// ---------------------------------------------------------------------------------------------
// ScaledGfx
// ---------------------------------------------------------------------------------------------

int ScaledGfx::cell_for(float design_size) const {
  return std::max(1, static_cast<int>(std::lround(design_size * sx_)));
}

void ScaledGfx::apply_style() {
  if (!t_ || !style_dirty_) return;
  style_dirty_ = false;
  const lgfx::IFont* mapped = map_font(font_);
  t_->setFont(mapped);
  // Font0 is a pixel font: only whole-pixel cells keep its shapes. Other fonts were already
  // mapped to a smaller face, so their size multiplier stays as the UI set it.
  t_->setTextSize(is_pixel_font(font_) ? static_cast<float>(cell_for(text_size_)) : text_size_);
  t_->setTextColor(fg_, bg_);
  t_->setTextDatum(datum_);
}

void ScaledGfx::mark_rect(int x, int y, int w, int h) {
  dirty(X(x), Y(y), X(x + w), Y(y + h));
}

int32_t ScaledGfx::textWidth(const char* text) {
  apply_style();
  return static_cast<int32_t>(std::lround(t_->textWidth(text) / sx_));
}
int32_t ScaledGfx::fontHeight() {
  apply_style();
  return static_cast<int32_t>(std::lround(t_->fontHeight() / sy_));
}


int32_t ScaledGfx::drawString(const char* text, int x, int y) {
  apply_style();
  const int py = Y(y);
  const int width = t_->drawString(text, X(x), py);
  const int fh = t_->fontHeight();
  dirty(0, py - fh, tw_, py + fh);
  return static_cast<int32_t>(std::lround(width / sx_));
}

void ScaledGfx::setClipRect(int x, int y, int w, int h) {
  const int px = X(x), py = Y(y);
  t_->setClipRect(px, py, std::max(1, X(x + w) - px), std::max(1, Y(y + h) - py));
}
void ScaledGfx::clearClipRect() { t_->clearClipRect(); }

void ScaledGfx::clearScrollRect() {
  has_scroll_ = false;
  vacated_ = 0;
  t_->clearScrollRect();
}
void ScaledGfx::scroll(int dx, int dy) {
  // Whole pixels, at least one when asked to move at all, never fewer than a fill row of the same
  // request occupies (so the strip the UI repaints always covers what scrolling vacated).
  const auto step = [](int d, float s) {
    if (d == 0) return 0;
    const int n = std::max(1, static_cast<int>(std::floor(std::abs(d) * s)));
    return d < 0 ? -n : n;
  };
  const int px = step(dx, sx_), py = step(dy, sy_);
  if (!px && !py) return;
  t_->scroll(px, py);
  vacated_ = py < 0 ? -py : 0;
  if (has_scroll_) dirty(scroll_x_, scroll_y_, scroll_x_ + scroll_w_, scroll_y_ + scroll_h_);
  else dirty(0, 0, tw_, th_);
}

void ScaledGfx::readRect(int x, int y, int w, int h, uint16_t* out) {
  const int px = X(x), py = Y(y), pw = std::max(1, X(x + w) - px), ph = std::max(1, Y(y + h) - py);
  uint16_t* tmp = static_cast<uint16_t*>(scaled_gfx_scratch(sizeof(uint16_t) * pw * ph));
  if (!tmp) return;
  t_->readRect(px, py, pw, ph, tmp);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i)
      out[static_cast<size_t>(j) * w + i] = tmp[static_cast<size_t>(j * ph / h) * pw + (i * pw / w)];
}

// ---------------------------------------------------------------------------------------------
// ScaledDisplay
// ---------------------------------------------------------------------------------------------

bool ScaledDisplay::begin(int target_w, int target_h, int design_w, int design_h) {
  sprite_.setPsram(true);
  sprite_.setColorDepth(16);
  if (!sprite_.createSprite(target_w, target_h)) {
    ESP_LOGE(kTag, "display sprite %dx%d allocation failed", target_w, target_h);
    return false;
  }
  bind(&sprite_, design_w, design_h, target_w, target_h);
  sprite_.fillScreen(0);
  ready_ = true;
  mark_all_dirty();
  ESP_LOGI(kTag, "display %dx%d (design %dx%d, scale %.3f x %.3f), RGB565 sprite %u bytes", target_w, target_h,
           design_w, design_h, sx_, sy_, unsigned(sprite_.bufferLength()));
  return true;
}

void ScaledDisplay::dirty(int x0, int y0, int x1, int y1) {
  (void)x0;
  (void)x1;
  y0 = std::clamp(y0, 0, th_);
  y1 = std::clamp(y1, 0, th_);
  if (y1 <= y0) return;
  if (dirty_y1_ <= dirty_y0_) {
    dirty_y0_ = y0;
    dirty_y1_ = y1;
  } else {
    dirty_y0_ = std::min(dirty_y0_, y0);
    dirty_y1_ = std::max(dirty_y1_, y1);
  }
}

void ScaledDisplay::mark_dirty_target(int x0, int y0, int x1, int y1) { dirty(x0, y0, x1, y1); }

bool ScaledDisplay::take_dirty(int* y0, int* y1) {
  if (dirty_y1_ <= dirty_y0_) return false;
  *y0 = dirty_y0_;
  *y1 = dirty_y1_;
  dirty_y0_ = dirty_y1_ = 0;
  return true;
}

// ---------------------------------------------------------------------------------------------
// ScaledCanvas
// ---------------------------------------------------------------------------------------------

bool ScaledCanvas::createSprite(int w, int h) {
  deleteSprite();
  const float sx = parent_ ? parent_->scale_x() : 1.0f, sy = parent_ ? parent_->scale_y() : 1.0f;
  const int tw = std::max(1, static_cast<int>(std::lround(w * sx))), th = std::max(1, static_cast<int>(std::lround(h * sy)));
  sprite_.setPsram(psram_);
  sprite_.setColorDepth(depth_);
  if (!sprite_.createSprite(tw, th)) {
    ESP_LOGE(kTag, "canvas %dx%d (target %dx%d) allocation failed", w, h, tw, th);
    return false;
  }
  bind(&sprite_, w, h, tw, th);
  sx_ = sx;  // keep the display's exact scale so coordinates inside the sprite match the screen's
  sy_ = sy;
  created_ = true;
  return true;
}

void ScaledCanvas::deleteSprite() {
  if (created_) sprite_.deleteSprite();
  created_ = false;
}


void ScaledCanvas::pushSprite(int x, int y) { pushSprite(parent_, x, y); }

void ScaledCanvas::pushSprite(ScaledDisplay* dst, int x, int y) {
  if (!created_ || !dst) return;
  const int px = dst->X(x), py = dst->Y(y);
  sprite_.pushSprite(dst->target(), px, py);
  dst->mark_dirty_target(px, py, px + tw_, py + th_);
}


}  // namespace orcsdr::gfx
