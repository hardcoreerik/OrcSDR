#pragma once
// OrcSDR preproduction dashboard: native M5GFX renderer, body only.
// Caller is the UI framebuffer owner, draws dashboard_audio_control shared chrome,
// and supplies immutable snapshots. No radio/SD/USB or decoder dependencies.
#include "digital_voice_dashboard_layout.hpp"
#include "digital_voice_dashboard_paint.hpp"
#include "digital_voice_dashboard_model.hpp"
#include "digital_voice_scan_view.hpp"
#include "digital_voice_live_monitor.hpp"
#include <M5Unified.h>

namespace orcsdr::digital_voice_ui {
class Dashboard final {
 public:
  // Use only after the owning UI task is granted the screen, with overlays closed.
  // Returns false without drawing if ownership or overlay constraints are not met.
  bool present(lgfx::LovyanGFX& display, const Snapshot& snapshot,
               const UiState& state, bool owns_screen, bool overlay_open);
  Action handle_touch(int32_t x, int32_t y, const Snapshot& snapshot,
                      const UiState& state) const;
  void leave() { ready_ = false; sys_ready_ = false; disc_ready_ = false; live_ready_ = false; scan_.release(); pb_.release(); graphs_.release(); }
  bool active() const { return ready_; }
  // Optional real telemetry for the LIVE monitor (spectrum, symbol levels, SNR / BER / deviation, directory hint). The owner keeps the object
  // alive and refreshes it on the UI task; nullptr (the default) means no producer: every plot then shows that it has no data.
  void set_plot(const live_monitor::PlotData* plot) { plot_ = plot; }
 private:
  // Pages other than SCAN and SYSTEMS repaint only when something they show has changed.
  void present_page(lgfx::LovyanGFX& display, const Snapshot& s, const UiState& state);
  bool present_main(lgfx::LovyanGFX& display, const Snapshot& snapshot, const UiState& state, bool owns_screen, bool overlay_open);
  // The LIVE monitor: five retained panels, the RF level fallback, a popup and the graphs, each repainted only when what it shows changed.
  void live_service(lgfx::LovyanGFX& display, const Snapshot& s, const UiState& state, bool entry);
  // Signature of the page the screen shows (see present_page); copying a Dashboard starts with no page shown.
  struct PageBuffers {
    bool valid = false;
    uint32_t sig = 0;
    PageBuffers() = default;
    PageBuffers(const PageBuffers&) {}
    PageBuffers& operator=(const PageBuffers&) { valid = false; return *this; }
    ~PageBuffers() { free_diff(); }
    void release();
    // What each widget drew last time (see paint::Diff): arrays in PSRAM, allocated on first use. Without them the page repaints as before.
    paint::Diff diff;
    bool ensure_diff();
    void free_diff();
  };
  PageBuffers pb_;
  Snapshot old_{};  // bounded ~1 KiB, no heap; review internal RAM budget before firmware integration
  UiState last_state_{};
  ScanView scan_{};        // retained SCAN widgets: what is on screen, to repaint only what changed
  uint32_t ctx_sig_ = 0;   // what the context strip currently shows
  uint32_t last_paint_ms_ = 0;   // when a page last repainted: value changes arriving faster than about 4 a second wait for the next slot
  bool ready_ = false;
  // Systems page: what each row and the right panel currently show, so only changed parts are repainted.
  uint32_t sys_row_sig_[4] = {};
  uint32_t sys_facts_sig_ = 0;
  uint32_t sys_shape_ = 0;
  bool sys_ready_ = false;
  // DISCOVER page: signatures of its parts (frame, progress, buttons, four rows) so only a changed part repaints
  uint32_t disc_sig_[7] = {};
  bool disc_ready_ = false;
  // LIVE monitor
  struct GraphBuf {
    live_monitor::Graphs* g = nullptr;     // ~5.8 KiB of waterfall history: PSRAM, allocated on first use, freed on leave
    GraphBuf() = default;
    GraphBuf(const GraphBuf&) {}
    GraphBuf& operator=(const GraphBuf&) { release(); return *this; }
    ~GraphBuf() { release(); }
    bool ensure();
    void release();
  };
  GraphBuf graphs_;
  const live_monitor::PlotData* plot_ = nullptr;
  uint32_t live_sig_[live_monitor::kPanelCount] = {};
  uint32_t live_ms_ = 0;
  uint8_t live_popup_ = 0;
  bool live_ready_ = false, live_trace_ok_ = false;
};
}
