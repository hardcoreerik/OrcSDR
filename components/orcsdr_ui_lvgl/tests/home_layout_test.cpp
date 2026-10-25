#include "orcsdr_ui/home_layout.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
using namespace orcsdr::ui::home;
#define CHECK(expr) do { if(!(expr)) { std::fprintf(stderr,"CHECK failed: %s:%d: %s\n",__FILE__,__LINE__,#expr); std::abort(); } } while(0)

static std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  CHECK(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// The design constants must equal the ones in the Tab5 source, so the port cannot silently drift.
static void check_constants_match_tab5() {
  const std::string home = slurp(std::string(TAB5_UI_DIR) + "/home_dashboard.cpp");
  const std::string header = slurp(std::string(TAB5_UI_DIR) + "/dashboard_audio_control.cpp") + slurp(std::string(TAB5_UI_DIR) + "/dashboard_audio_control.hpp");
  struct Item { const char* name; int value; bool in_header; };
  using namespace design;
  const std::vector<Item> items = {
    {"kRailX",kRailX,false},{"kRailY",kRailY,false},{"kRailW",kRailW,false},{"kRailH",kRailH,false},
    {"kMainX",kMainX,false},{"kMainY",kMainY,false},{"kMainW",kMainW,false},{"kMainH",kMainH,false},
    {"kPlotX",kPlotX,false},{"kPlotW",kPlotW,false},{"kSpectrumY",kSpectrumY,false},{"kSpectrumH",kSpectrumH,false},
    {"kWaterfallY",kWaterfallY,false},{"kWaterfallH",kWaterfallH,false},{"kReadoutY",kReadoutY,false},{"kReadoutH",kReadoutH,false},
    {"kContrastDownX",kContrastDownX,false},{"kContrastUpX",kContrastUpX,false},{"kModeX",kModeX,false},{"kModeW",kModeW,false},
    {"kPaletteX",kPaletteX,false},{"kPaletteW",kPaletteW,false},{"kSpeedX",kSpeedX,false},{"kSpeedW",kSpeedW,false},
    {"kControlY",kControlY,false},{"kControlH",kControlH,false},{"kStepperW",kStepperW,false},{"kStepperSpan",kStepperSpan,false},
    {"kStepperTune",kStepperTune,false},{"kStepperSize",kStepperSize,false},{"kStepperArrowW",kStepperArrowW,false},
    {"kStepperArrowH",kStepperArrowH,false},{"kStepperArrowInset",kStepperArrowInset,false},
    {"kContrastButtonW",kContrastButtonW,false},{"kContrastButtonH",kContrastButtonH,false},
    {"kListX",kListX,false},{"kListY",kListY,false},{"kListW",kListW,false},{"kListH",kListH,false},
    {"kRowH",kRowH,false},{"kRowGap",kRowGap,false},{"kVisibleRows",kVisibleRows,false},{"kAllY",kAllY,false},
    {"kGainX",kGainX,false},{"kGainY",kGainY,false},{"kGainW",kGainW,false},{"kGainH",kGainH,false},
    {"kFilterPanelX",kFilterPanelX,false},{"kFilterPanelY",kFilterPanelY,false},{"kFilterPanelW",kFilterPanelW,false},{"kFilterPanelH",kFilterPanelH,false},
    {"kStatusBarX",kStatusBarX,true},{"kStatusBarY",kStatusBarY,true},{"kStatusBarW",kStatusBarW,true},{"kStatusBarH",kStatusBarH,true},
    {"kRegionX",kRegionX,true},{"kRegionY",kRegionY,true},{"kRegionW",kRegionW,true},{"kRegionH",kRegionH,true},
    {"kHomeX",kHomeX,true},{"kHomeY",kHomeY,true},{"kHomeW",kHomeW,true},{"kHomeH",kHomeH,true},
    {"kMuteX",kMuteX,true},{"kMuteY",kMuteY,true},{"kMuteW",kMuteW,true},{"kMuteH",kMuteH,true},
    {"kVisualizerX",kVisualizerX,true},{"kVisualizerY",kVisualizerY,true},{"kVisualizerW",kVisualizerW,true},{"kVisualizerH",kVisualizerH,true},
    {"kSettingsX",kSettingsX,true},{"kSettingsY",kSettingsY,true},{"kSettingsW",kSettingsW,true},{"kSettingsH",kSettingsH,true},
  };
  for (const Item& it : items) {
    const std::string& src = it.in_header ? header : home;
    const std::regex re(std::string("\\b") + it.name + "\\s*=\\s*(\\d+)");
    std::smatch m;
    if (!std::regex_search(src, m, re)) {
      std::fprintf(stderr, "constant %s not found in the Tab5 source\n", it.name);
      std::abort();
    }
    if (std::stoi(m[1].str()) != it.value) {
      std::fprintf(stderr, "constant %s: Tab5 source has %s, mirror has %d\n", it.name, m[1].str().c_str(), it.value);
      std::abort();
    }
  }
  std::printf("PASS %zu design constants match the Tab5 source\n", items.size());
}

static bool inside(Rect big, Rect r) { return r.valid() && r.x >= big.x && r.y >= big.y && r.right() <= big.right() && r.bottom() <= big.bottom(); }
static bool overlap(Rect a, Rect b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

static void check_identity() {
  const Layout l = plan(1280, 720);
  CHECK(l.valid);
  CHECK(l.rail.x == 24 && l.rail.y == 112 && l.rail.w == 280 && l.rail.h == 530);
  CHECK(l.spectrum.x == 330 && l.spectrum.w == 906 && l.spectrum.h == 186);
  CHECK(l.stepper[2].x == 758 && l.stepper[2].w == 208 && l.stepper[2].h == 62);
  CHECK(l.row_h == 52 && l.row_gap == 8);
  std::puts("PASS 1280x720 is the identity mapping");
}

static void check_display(int w, int h) {
  const Layout l = plan(w, h);
  CHECK(l.valid);
  const Rect screen{0, 0, w, h};
  const Rect* all[] = {&l.outer, &l.badge, &l.status_bar, &l.battery, &l.home_button, &l.mute_button,
                       &l.visualizer_button, &l.settings_button, &l.rail, &l.list, &l.all_button,
                       &l.main_frame, &l.spectrum, &l.waterfall, &l.frequency, &l.contrast_down, &l.contrast_up,
                       &l.palette, &l.speed, &l.mode, &l.filter, &l.gain, &l.footer};
  for (const Rect* r : all) CHECK(inside(screen, *r));
  for (const Rect& s : l.stepper) CHECK(inside(screen, s));
  // Independent regions do not overlap.
  const Rect* controls[] = {&l.home_button, &l.mute_button, &l.visualizer_button, &l.settings_button,
                            &l.status_bar, &l.rail, &l.spectrum, &l.waterfall, &l.palette, &l.speed,
                            &l.mode, &l.filter, &l.gain, &l.footer, &l.contrast_down, &l.contrast_up};
  const int n = sizeof(controls) / sizeof(controls[0]);
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j) CHECK(!overlap(*controls[i], *controls[j]));
  for (int i = 0; i < 3; ++i)
    for (int j = i + 1; j < 3; ++j) CHECK(!overlap(l.stepper[i], l.stepper[j]));
  for (const Rect& s : l.stepper) CHECK(!overlap(s, l.filter) && !overlap(s, l.gain) && !overlap(s, l.footer));
  CHECK(!overlap(l.spectrum, l.waterfall) && !overlap(l.waterfall, l.frequency));
  CHECK(l.list.h >= l.visible_rows * (l.row_h + l.row_gap) - l.row_gap);
  // Touch targets: hit areas are at least kMinTouch and stay on screen; the visual stays inside its hit area.
  const Rect* touch[] = {&l.home_button, &l.mute_button, &l.visualizer_button, &l.settings_button,
                         &l.contrast_down, &l.contrast_up, &l.palette, &l.speed, &l.mode, &l.filter, &l.gain, &l.all_button};
  for (const Rect* t : touch) {
    const Rect hit = hit_area(*t, w, h);
    CHECK(inside(screen, hit) && hit.w >= kMinTouch && hit.h >= kMinTouch);
    CHECK(inside(hit, *t));
  }
  std::printf("PASS %dx%d geometry\n", w, h);
}

static void check_800x480_details() {
  const Layout l = plan(800, 480);
  std::printf("INFO 800x480: rail %dx%d, spectrum %dx%d, waterfall %dx%d, stepper %dx%d, arrow %dx%d, row %d+%d\n",
              l.rail.w, l.rail.h, l.spectrum.w, l.spectrum.h, l.waterfall.w, l.waterfall.h, l.stepper[0].w,
              l.stepper[0].h, scale_x(design::kStepperArrowW, 800), scale_y(design::kStepperArrowH, 480), l.row_h,
              l.row_gap);
  CHECK(l.spectrum.w >= 560 && l.spectrum.h >= 120);
  CHECK(l.waterfall.h >= 120);
  // Text scale: Tab5 sizes 1..5 on 800 wide.
  CHECK(cell_for_size(1, 800) == 1 && cell_for_size(2, 800) == 1 && cell_for_size(3, 800) == 2);
  CHECK(cell_for_size(4, 800) == 3 && cell_for_size(5, 800) == 3);
  CHECK(cell_for_size(2, 1280) == 2 && cell_for_size(4, 1280) == 4);
  std::puts("PASS 800x480 details");
}

int main() {
  check_constants_match_tab5();
  check_identity();
  check_display(800, 480);
  check_display(1280, 720);
  check_display(1024, 600);
  check_display(854, 480);
  check_800x480_details();
  CHECK(!plan(320, 240).valid);
  std::puts("PASS all");
}
