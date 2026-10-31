#pragma once
// M5GFX presentation-only Weather Fax prototype. Not yet routed from main.cpp.
// Caller owns screen lifecycle, tuning, decoder, storage and verified UTC.
#include <cstdint>
namespace orcsdr::wefax_dashboard {
struct Snapshot {
  uint64_t verified_utc_epoch = 0; // 0 means NOT VERIFIED; never show a false countdown
  uint32_t tuned_hz = 0;
  uint32_t revision = 0;          // optional: decoder view revision
  uint16_t received_rows = 0;
  bool receiver_ready = false;
  bool wefax_receiving = false;
  bool decoder_connected = false; // true ONLY after wiring production PCM
};
enum class ActionKind : uint8_t { none, tune_usb };
struct Action { ActionKind kind = ActionKind::none; uint32_t dial_hz = 0; };
// Called on the display UI task only. enter() is the only method allowed to
// redraw chrome; update() paints changed body regions only.
void enter(const Snapshot& snapshot);
void update(const Snapshot& snapshot);
void leave();
Action touch(int32_t x, int32_t y); // no direct tuner calls
void set_header_hook(void (*hook)());
bool active();
} // namespace orcsdr::wefax_dashboard
