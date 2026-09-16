#pragma once

#include <algorithm>
#include <cstddef>

namespace orcsdr::spectrum {

inline float peak_for_pixel(const float* levels, size_t first_bin,
                            size_t visible_bins, size_t pixel,
                            size_t pixel_count) {
  const size_t first = first_bin + pixel * visible_bins / pixel_count;
  const size_t last = std::max(first + 1,
      first_bin + (pixel + 1) * visible_bins / pixel_count);
  float peak = levels[first];
  for (size_t bin = first + 1; bin < last; ++bin)
    peak = std::max(peak, levels[bin]);
  return peak;
}

}  // namespace orcsdr::spectrum
