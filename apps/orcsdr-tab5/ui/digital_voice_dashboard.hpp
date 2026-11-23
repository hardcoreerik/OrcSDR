#pragma once
// OrcSDR preproduction dashboard: native M5GFX renderer, body only.
// Caller is the UI framebuffer owner, draws dashboard_audio_control shared chrome,
// and supplies immutable snapshots. No radio/SD/USB or decoder dependencies.
#include "digital_voice_dashboard_layout.hpp"
#include "digital_voice_dashboard_model.hpp"
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
  void leave() { ready_ = false; }
  bool active() const { return ready_; }
 private:
  Snapshot old_{};  // bounded ~1 KiB, no heap; review internal RAM budget before firmware integration
  UiState last_state_{};
  bool ready_ = false;
};
}
