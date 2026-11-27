#pragma once
// Adapter for OrcSDR's one-framebuffer ScreenController contract.
// The owning UI task supplies genuine decoder/receiver snapshots and shared-header painter.
#include "digital_voice_dashboard.hpp"

namespace orcsdr::digital_voice_ui {
using HeaderHook = void (*)();

class Surface final {
 public:
  void enter() { dashboard_.leave(); state_ = {}; }
  void leave() { dashboard_.leave(); }
  // Called only after the app's screen transition has finished; draws shared header once.
  // Never paints while the volume/modal overlay is open or another screen is active.
  bool draw(lgfx::LovyanGFX& display, const Snapshot& snapshot,
            bool overlay_open, HeaderHook shared_header);
  // Must run on the owning UI task; model revision gates unchanged paints.
  bool update(lgfx::LovyanGFX& display, const Snapshot& snapshot, bool overlay_open);
  // Navigation and density apply locally. Receiver/scan/storage actions are RETURNED
  // to the app, never directly executed here. No radio or USB ownership.
  Action touch(int32_t x, int32_t y, const Snapshot& snapshot, bool overlay_open);
  const UiState& state() const { return state_; }
  void set_plot(const live_monitor::PlotData* plot) { dashboard_.set_plot(plot); }
  bool active() const { return dashboard_.active(); }

 private:
  Dashboard dashboard_{};
  UiState state_{};
};
}
