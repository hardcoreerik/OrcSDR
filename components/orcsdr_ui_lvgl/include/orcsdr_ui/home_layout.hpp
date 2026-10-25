#pragma once
// Geometry of the real OrcSDR Home screen for any display size, with no graphics-library
// dependency (host-testable).
//
// The Tab5 implementation (apps/orcsdr-tab5/ui/home_dashboard.cpp and
// dashboard_audio_control.cpp) places everything with pixel constants for a 1280x720
// panel. This module keeps those constants as the "design space" and maps every
// rectangle onto the target display by scaling each axis independently, rounding
// edges (not sizes) so neighbouring rectangles stay flush. Content is the same, in the
// same places, at the same proportions; only the pixel grid changes.
//
// tests/home_layout_test.cpp checks the mirrored constants against the Tab5 source text.
#include <cstdint>

namespace orcsdr::ui::home {

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  constexpr int right() const { return x + w; }
  constexpr int bottom() const { return y + h; }
  constexpr bool valid() const { return w > 0 && h > 0; }
  constexpr bool contains(int px, int py) const { return px >= x && py >= y && px < right() && py < bottom(); }
};
struct Point {
  int x = 0, y = 0;
};

constexpr int kDesignW = 1280, kDesignH = 720;

// Design-space constants copied from the Tab5 source (names match home_dashboard.cpp /
// dashboard_audio_control.cpp). Verified by tests/home_layout_test.cpp.
namespace design {
constexpr int kRailX = 24, kRailY = 112, kRailW = 280, kRailH = 530;
constexpr int kMainX = 318, kMainY = 80, kMainW = 930, kMainH = 562;
constexpr int kPlotX = 330, kPlotW = 906;
constexpr int kSpectrumY = 112, kSpectrumH = 186;
constexpr int kWaterfallY = 301, kWaterfallH = 194;
constexpr int kReadoutY = 504, kReadoutH = 52;
constexpr int kContrastDownX = 690, kContrastUpX = 854;
constexpr int kModeX = 1144, kModeW = 92;
constexpr int kPaletteX = 898, kPaletteW = 120;
constexpr int kSpeedX = 1026, kSpeedW = 110;
constexpr int kControlY = 564, kControlH = 62;
constexpr int kStepperW = 208, kStepperSpan = 330, kStepperTune = 544, kStepperSize = 758;
constexpr int kStepperArrowW = 34, kStepperArrowH = 48, kStepperArrowInset = 4;
constexpr int kContrastButtonW = 36, kContrastButtonH = 28;
constexpr int kListX = 30, kListY = 158, kListW = 242, kListH = 420;
constexpr int kRowH = 52, kRowGap = 8;
constexpr int kVisibleRows = 7;
constexpr int kAllY = 590;
constexpr int kGainX = 1124, kGainY = 564, kGainW = 112, kGainH = 62;
constexpr int kFilterPanelX = 976, kFilterPanelY = 564, kFilterPanelW = 138, kFilterPanelH = 62;
// audio_header (dashboard_audio_control.cpp)
constexpr int kStatusBarX = 420, kStatusBarY = 12, kStatusBarW = 446, kStatusBarH = 54;
constexpr int kRegionX = 866, kRegionY = 12, kRegionW = 174, kRegionH = 54;
constexpr int kHomeX = 1040, kHomeY = 8, kHomeW = 58, kHomeH = 58;
constexpr int kMuteX = 1099, kMuteY = 12, kMuteW = 54, kMuteH = 54;
constexpr int kVisualizerX = 1158, kVisualizerY = 12, kVisualizerW = 54, kVisualizerH = 54;
constexpr int kSettingsX = 1217, kSettingsY = 12, kSettingsW = 51, kSettingsH = 54;
}  // namespace design

struct Layout {
  bool valid = false;
  int width = 0, height = 0;
  Rect outer;                         // the cyan screen border
  Rect badge;                         // logo
  Point brand, subtitle;              // "OrcSDR" and the page title (middle-left anchors)
  Rect status_bar;                    // Wi-Fi / RTL-SDR / clock chip
  Rect battery;                       // battery region (percent text + battery icon)
  Rect home_button, mute_button, visualizer_button, settings_button;
  Rect rail, list, all_button;
  Point last_used_label;              // "LAST USED"
  Rect last_used_rule;                // the line after the label
  int row_h = 0, row_gap = 0, visible_rows = 0;
  Rect main_frame;
  Point spectrum_label, band_label;   // "SPECTRUM" (left) and the band name (centred)
  Rect spectrum, waterfall;
  Rect frequency;                     // readout (tap = keypad)
  Rect contrast_down, contrast_up;
  Rect palette, speed, mode;
  Rect stepper[3];                    // SPAN, TUNE, STEP SIZE
  Rect filter, gain;
  Rect footer;
  int footer_divider_x[5] = {};
  Rect footer_cell[6];                // gain, bias, receiver, sample rate, bandwidth, level
  int footer_text_y = 0;
};

// Scales a design-space rectangle onto a width x height display (edge rounding).
Rect scale_rect(Rect design, int width, int height);
Point scale_point(Point design, int width, int height);
int scale_x(int design, int width);
int scale_y(int design, int height);

// Plans Home for the display. Valid for any display at least 640x360.
Layout plan(int width, int height);

// Smallest touch target (in pixels) the UI should offer; see hit_area().
constexpr int kMinTouch = 44;
// The rectangle that should receive taps for a control drawn at `visual`: the visual
// rectangle grown (symmetrically, clamped to the display) up to kMinTouch on each axis.
Rect hit_area(Rect visual, int width, int height);

// Text scale: Tab5 text size n (n x n pixels per font pixel) -> whole-pixel cell used here.
int cell_for_size(int tab5_size, int width);

}  // namespace orcsdr::ui::home
