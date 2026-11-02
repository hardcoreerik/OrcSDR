#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include "orcsdr_gfx/dirty_rows.hpp"

namespace orcsdr::gfx {
class ScaledDisplay {
 public:
  static constexpr std::size_t kMaxDirtyRanges = 16;
  class SpriteProbe {
   public:
    explicit SpriteProbe(ScaledDisplay* owner = nullptr) : owner_(owner) {}
    void drawPixel(int x, int y, uint16_t color) {
      if (owner_ && x >= 0 && y >= 0 && x < owner_->width_ && y < owner_->height_)
        owner_->pixels_[static_cast<std::size_t>(y) * owner_->width_ + x] = color;
    }
   private:
    ScaledDisplay* owner_;
  };

  bool ready() const { return ready_; }
  int target_width() const { return width_; }
  int target_height() const { return height_; }
  const uint16_t* buffer() const { return pixels_.data(); }
  uint16_t* mutable_buffer() { return pixels_.data(); }
  std::size_t stride_pixels() const { return static_cast<std::size_t>(width_); }
  SpriteProbe sprite() { return SpriteProbe(this); }
  bool begin(int width, int height, int, int) {
    if (width <= 0 || height <= 0 || width > kMaxWidth || height > kMaxHeight) return false;
    width_ = width;
    height_ = height;
    ready_ = true;
    std::fill(pixels_.begin(), pixels_.end(), 0);
    dirty_.reset(height_);
    return true;
  }
  void mark_all_dirty() { dirty_.clear(); dirty_.add(0, height_); }
  void add_dirty(int y0, int y1) { dirty_.add(y0, y1); }
  bool take_dirty(int* y0, int* y1) {
    if (!y0 || !y1) return false;
    return dirty_.take(*y0, *y1);
  }
  bool dirty_empty() const { return dirty_.empty(); }

 private:
  static constexpr int kMaxWidth = 800, kMaxHeight = 800;
  std::array<uint16_t, kMaxWidth * kMaxHeight> pixels_{};
  DirtyRows<kMaxDirtyRanges> dirty_{};
  int width_ = 800, height_ = 480;
  bool ready_ = true;
};
}  // namespace orcsdr::gfx
