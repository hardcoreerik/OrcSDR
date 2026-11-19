#pragma once
// UI-only CW/RTTY Skimmer preview for the OrcSDR Tab5 (1280x720, M5GFX).
// Decoder workers publish bounded Snapshots; only the UI task draws M5GFX pixels.
// No RF backend or tuning path is enabled by this module.
#include <cstddef>
#include <cstdint>

namespace orcsdr::skimmer {

enum class Mode : uint8_t { cw, rtty, both };
enum class Tab : uint8_t { live, stations, decodes, setup };
enum class Source : uint8_t { unavailable, demo, live };

constexpr size_t kMaxSignals = 12;
struct Signal {
  uint32_t frequency_hz = 0;
  Mode mode = Mode::cw;
  int16_t snr_db = 0;
  uint8_t confidence = 0;
  bool decoded = false;
  char callsign[16]{};
  char message[80]{};
};

struct Snapshot {
  Source source = Source::unavailable;
  uint32_t center_hz = 14030000;
  uint32_t span_hz = 40000;
  uint32_t sample_revision = 0;
  uint32_t signal_revision = 0;
  uint8_t count = 0;
  Signal signals[kMaxSignals]{};
};

void enter();
void leave();
bool active();
Tab tab();
Mode filter();
Source source();
void draw();                   // Full layout redraw only on entry/tab changes
void update();                 // Dirty-region updates; no full-screen clear
bool handle_touch(int32_t x, int32_t y);
void set_snapshot(const Snapshot& data);
void show_demo(bool enable);   // Explicitly labelled UI preview, never RF
void set_header_hook(void (*draw_controls)());

}  // namespace orcsdr::skimmer
