#pragma once
// Screen-specific 720 x 720 Home geometry. NOT a scaled Tab5 dashboard.
#include <cstdint>
namespace orcsdr::waveshare4b {
struct Rect {
  int x, y, w, h;
  constexpr bool inside(int W = 720, int H = 720) const {
    return x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= W && y + h <= H;
  }
  constexpr bool disjoint(Rect b) const {
    return x + w <= b.x || b.x + b.w <= x || y + h <= b.y || b.y + b.h <= y;
  }
};
constexpr int kWidth = 720, kHeight = 720;
constexpr Rect kHeader{16, 12, 688, 52};
constexpr Rect kStatus{16, 72, 688, 34};
constexpr Rect kTuner{16, 114, 688, 82};
constexpr Rect kSpectrumPanel{16, 206, 688, 150};
constexpr Rect kSpectrum{28, 230, 664, 118};
constexpr Rect kWaterfallPanel{16, 366, 688, 210};
constexpr Rect kWaterfall{28, 390, 664, 174};
constexpr Rect kControls{16, 586, 688, 118};
constexpr Rect kPreset1{24, 594, 160, 48};
constexpr Rect kPreset2{194, 594, 160, 48};
constexpr Rect kPreset3{364, 594, 160, 48};
constexpr Rect kPreset4{534, 594, 160, 48};
constexpr Rect kTuneDown{24, 650, 120, 48};
constexpr Rect kStep{154, 650, 412, 48};
constexpr Rect kTuneUp{576, 650, 118, 48};
constexpr Rect kTunerMinus{26, 128, 62, 54};
constexpr Rect kTunerReadout{94, 128, 530, 54};
constexpr Rect kTunerPlus{632, 128, 62, 54};
constexpr uint32_t kDefaultHz = 96100000u;
constexpr uint32_t kMinHz = 24000u, kMaxHz = 1766000000u;
constexpr uint32_t kSteps[] = {1000u, 5000u, 12500u, 25000u, 100000u, 1000000u};
constexpr uint32_t tune(uint32_t hz, int direction, uint32_t step) {
  return direction > 0 ? (hz > kMaxHz - step ? kMaxHz : hz + step)
                       : (hz < kMinHz + step ? kMinHz : hz - step);
}
static_assert(kHeader.inside() && kStatus.inside() && kTuner.inside() &&
              kSpectrumPanel.inside() && kWaterfallPanel.inside() && kControls.inside(),
              "Home sections must fit 720x720");
static_assert(kSpectrum.inside() && kWaterfall.inside() &&
              kPreset1.inside() && kPreset2.inside() && kPreset3.inside() &&
              kPreset4.inside() && kTuneDown.inside() && kStep.inside() && kTuneUp.inside(),
              "Plots and touch targets must be inside viewport");
static_assert(kHeader.disjoint(kStatus) && kStatus.disjoint(kTuner) &&
              kTuner.disjoint(kSpectrumPanel) && kSpectrumPanel.disjoint(kWaterfallPanel) &&
              kWaterfallPanel.disjoint(kControls), "Screen regions must not overlap");
static_assert(kPreset1.disjoint(kPreset2) && kPreset2.disjoint(kPreset3) &&
              kPreset3.disjoint(kPreset4) && kTuneDown.disjoint(kStep) &&
              kStep.disjoint(kTuneUp), "Home touch hitboxes cannot overlap");
} // namespace orcsdr::waveshare4b
