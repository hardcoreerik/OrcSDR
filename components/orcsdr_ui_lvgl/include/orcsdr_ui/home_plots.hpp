#pragma once
// Spectrum trace and waterfall rows for the Home screen, drawn into plain RGB565 buffers.
// A port of draw_spectrum() in apps/orcsdr-tab5/ui/home_dashboard.cpp: the same per-pixel
// smoothing, peak-hold dots, rising/falling ceiling, grid, centre line, filter edges and
// frequency axis. No graphics-library dependency, so it is tested on the host.
#include <cstddef>
#include <cstdint>

namespace orcsdr::ui::plots {

// M5GFX colours used by the Home plots (RGB565).
constexpr uint16_t kBlack = 0x0000, kDim = 0x4228, kGreen = 0x6FE0, kPeak = 0xFD20, kYellow = 0xFFE0,
                   kLightGrey = 0xD69A;

struct Surface {
  uint16_t* px = nullptr;
  int w = 0, h = 0;
  size_t stride_px = 0;  // pixels per row
};

void fill(Surface s, uint16_t color);
void set_pixel(Surface s, int x, int y, uint16_t color);
void hline(Surface s, int x, int y, int w, uint16_t color);
void vline(Surface s, int x, int y, int h, uint16_t color);
void line(Surface s, int x0, int y0, int x1, int y1, uint16_t color);
// Draws `text` in the 6x8 pixel font (Font0), each font pixel `cell` x `cell`. (x, y) is the top left.
void text(Surface s, int x, int y, const char* text, int cell, uint16_t color);
int text_width(const char* text, int cell);

struct SpectrumView {
  uint32_t frequency_hz = 0, span_hz = 0;
  uint32_t filter_bandwidth_hz = 0;
  bool show_edges = false;       // draw the receive filter edges
  int axis_band_px = 17;         // bottom band reserved for the frequency labels
  int axis_cell = 1;             // pixel-font cell of the axis labels
  int design_w = 904, design_h = 184;  // the plot interior on the Tab5 (grid spacing reference)
};

// One-spectrum state: per-pixel smoothing and peak hold. Sized for a plot up to kMaxPixels wide.
class Spectrum {
 public:
  static constexpr int kMaxPixels = 1024;
  // Renders one frame into `out` (the plot interior). `elapsed_ms` is the time since the previous
  // frame (clamped inside). Returns false if the arguments are unusable.
  bool render(Surface out, const float* levels, size_t first_bin, size_t visible_bins, float floor,
              uint32_t elapsed_ms, const SpectrumView& view);
  // The smoothed level of pixel x after the last render (used to colour the waterfall row).
  float smoothed(int x) const { return smooth_[x]; }
  float floor_db() const { return last_floor_; }
  void reset() { valid_ = false; ceiling_valid_ = false; }

 private:
  float smooth_[kMaxPixels] = {};
  float peak_[kMaxPixels] = {};
  bool valid_ = false, ceiling_valid_ = false;
  uint32_t last_frequency_ = 0, last_span_ = 0;
  size_t last_first_bin_ = 0, last_visible_ = 0;
  float ceiling_ = 0.0f, last_floor_ = 0.0f;
};

// Waterfall colour mapping range (dB) for a contrast setting 1..7, as in the Tab5.
inline uint8_t waterfall_range_db(uint8_t contrast) {
  const uint8_t c = contrast < 1 ? 1 : (contrast > 7 ? 7 : contrast);
  return static_cast<uint8_t>(54 - c * 6);
}

// Scrolls the waterfall up by `rows` rows; the newest rows then enter at the bottom.
void waterfall_scroll(Surface s, int rows);
// Fills the bottom `rows` rows from the spectrum's smoothed levels (two pixels at a time, as the Tab5 does).
// `color` maps a 0..1 level to RGB565.
using ColorFn = uint16_t (*)(float normalized);
void waterfall_fill_row(Surface s, int rows, const Spectrum& spectrum, float floor_db, float range_db,
                        ColorFn color);

}  // namespace orcsdr::ui::plots
