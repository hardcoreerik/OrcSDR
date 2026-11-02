#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace orcsdr::gfx {

// Bounded half-open dirty-row intervals. Adjacent ranges are coalesced so a
// row presenter can drain the earliest changed span without losing updates.
template <std::size_t Capacity = 16>
class DirtyRows {
  static_assert(Capacity > 0, "DirtyRows needs at least one interval slot");

 public:
  DirtyRows() = default;
  explicit DirtyRows(int height) { reset(height); }

  void reset(int height) {
    height_ = std::clamp(height, 0, 32767);
    clear();
  }

  void clear() { count_ = 0; }

  // Adds [y0,y1), clipped to the active display height. Empty intervals are ignored.
  bool add(int y0, int y1) {
    int begin = std::clamp(y0, 0, height_);
    int end = std::clamp(y1, 0, height_);
    if (begin >= end) return false;

    std::size_t first = 0;
    while (first < count_ && ranges_[first].end < begin) ++first;
    std::size_t last = first;
    while (last < count_ && ranges_[last].begin <= end) {
      begin = std::min(begin, ranges_[last].begin);
      end = std::max(end, ranges_[last].end);
      ++last;
    }

    if (first < last) {
      ranges_[first] = {begin, end};
      const std::size_t removed = last - first;
      const std::size_t tail = count_ - last;
      for (std::size_t i = 0; i < tail; ++i) ranges_[first + 1 + i] = ranges_[last + i];
      count_ -= removed - 1;
      return true;
    }

    if (count_ == Capacity) {
      // Preserve coverage under pressure; extra rows may be repainted.
      begin = std::min(begin, ranges_[0].begin);
      end = std::max(end, ranges_[count_ - 1].end);
      ranges_[0] = {begin, end};
      count_ = 1;
      return true;
    }

    for (std::size_t i = count_; i > first; --i) ranges_[i] = ranges_[i - 1];
    ranges_[first] = {begin, end};
    ++count_;
    return true;
  }

  // Drains the earliest interval, leaving the remaining intervals queued.
  bool take(int& y0, int& y1) {
    if (!count_) return false;
    y0 = ranges_[0].begin;
    y1 = ranges_[0].end;
    for (std::size_t i = 1; i < count_; ++i) ranges_[i - 1] = ranges_[i];
    --count_;
    return true;
  }

  bool empty() const { return count_ == 0; }
  std::size_t size() const { return count_; }
  int height() const { return height_; }

 private:
  struct Range { int begin = 0, end = 0; };
  std::array<Range, Capacity> ranges_{};
  std::size_t count_ = 0;
  int height_ = 0;
};

}  // namespace orcsdr::gfx
