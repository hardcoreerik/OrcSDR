#pragma once
// SCAN view of the Digital Voice dashboard, drawn as retained widgets. Screen entry paints the whole layout once; afterwards a change
// repaints only the widgets whose displayed inputs changed, each inside its own bounded rectangle (see docs/UI_RENDERING_STANDARD.md).
// Draw-only: it reads an immutable Snapshot and returns Actions; it never touches the receiver, the scanner or storage.
#include "digital_voice_dashboard_layout.hpp"
#include "digital_voice_dashboard_model.hpp"
#include <M5Unified.h>

namespace orcsdr::digital_voice_ui {

namespace scan_layout {
using layout::Rect;
struct Geometry {
  Rect a, b;                                   // the two cards
  Rect status, freq, plan, bar, counts, ladder, reason;
  Rect start, hold, skip, save, plan_prev, plan_next, discover;
  Rect mode, trace;                            // the mode badge (what kind of scan this is) and the RF level strip; trace is empty in the analysis density
  Rect row[kScanHits];
  Rect detail;
  int list_title_y, detail_title_y, footer_y;
};
Geometry geometry(Density d);
}  // namespace scan_layout

class ScanView {
 public:
  static constexpr int kWidgets = 6 + 6 + 1 + static_cast<int>(kScanHits) + 1 + 1 + 2;   // counted in the .cpp (static_assert there)
  ScanView() = default;
  ~ScanView();
  ScanView(const ScanView&) {}
  ScanView& operator=(const ScanView&) { release(); return *this; }
  void reset() { entered_ = false; }
  void release();
  bool entered() const { return entered_; }
  // Paint everything (static layout + every widget). Returns the number of widgets painted.
  int enter(lgfx::LovyanGFX& g, const Snapshot& s, const UiState& u);
  // Repaint only widgets whose inputs changed since the last call. Returns the number repainted (0 = nothing drawn).
  int update(lgfx::LovyanGFX& g, const Snapshot& s, const UiState& u);
  Action touch(int32_t x, int32_t y, const Snapshot& s, const UiState& u) const;

 private:
  int paint(lgfx::LovyanGFX& g, const Snapshot& s, const UiState& u, bool force);
  uint32_t sig_[kWidgets + 4]{};
  bool entered_ = false;
#if defined(ESP_PLATFORM)
  lgfx::LGFX_Sprite* wide_spr_ = nullptr;   // reusable 800x68 PSRAM buffer for flicker-free left-card widget pushes
  lgfx::LGFX_Sprite* tall_spr_ = nullptr;   // reusable 610x220 PSRAM buffer for right-card rows & detail panel
  lgfx::LGFX_Sprite* sprite_for(layout::Rect r);
#endif
};

}  // namespace orcsdr::digital_voice_ui
