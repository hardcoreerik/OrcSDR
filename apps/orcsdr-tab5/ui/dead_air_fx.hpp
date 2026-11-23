#pragma once
// The Dead Air "orb": a small animated cue that makes a measured event impossible to miss. Pure animation logic (no
// display): it turns "an event of this kind and this many dB above baseline happened at time T" into a frame
// description, and the dashboard draws the frame.
//
// What the orb means, and what it does not:
//  - Idle, it glows with how close the live RF level is to the event limit (a measurement); it is dim and still when no
//    level is being measured.
//  - On an event it bursts: rings and sparks whose count scales with the measured rise in dB. A mark you made bursts
//    too, in its own colour. It is a cue that a logged measurement happened, never a claim about its cause.
#include <cstddef>
#include <cstdint>

namespace orcsdr::dead_air {

enum class FxKind : uint8_t { none, rf, audio, mark };
constexpr size_t kFxSparks = 16;
constexpr size_t kFxRings = 3;

struct FxFrame {
  bool burst = false;           // an event animation is playing
  float core = 0.3f;            // orb radius, 0..1 of the box half-size
  float glow = 0.0f;            // 0..1: how bright the core is (idle proximity or burst flash)
  bool live = false;            // a level is being measured
  FxKind kind = FxKind::none;   // kind of the burst in progress
  uint8_t ring_count = 0;
  float ring_r[kFxRings]{};     // 0..1 of the box half-size
  float ring_a[kFxRings]{};     // alpha 0..1
  uint8_t spark_count = 0;
  float spark_x[kFxSparks]{}, spark_y[kFxSparks]{};  // offsets from the centre, -1..1
  float spark_a[kFxSparks]{};
};

class Fx {
 public:
  static constexpr uint32_t kBurstMs = 2600;
  // `rise_db` is the measured rise above baseline; ignored for a mark. A new event restarts the animation.
  void trigger(FxKind kind, float rise_db, uint32_t sequence, uint32_t now_ms);
  // `proximity` is 0..1: how close the live level is to the event limit. `live` is false when nothing is measured.
  void set_idle(bool live, float proximity);
  bool live() const { return live_; }
  bool animating(uint32_t now_ms) const;
  FxFrame frame(uint32_t now_ms) const;
  void reset();

 private:
  FxKind kind_ = FxKind::none;
  uint32_t start_ms_ = 0, sequence_ = 0;
  float intensity_ = 0.0f;  // 0.2..1
  uint8_t sparks_ = 0, rings_ = 0;
  bool burst_ = false, live_ = false;
  float proximity_ = 0.0f;
};

// Accent colour of an event kind (RGB565): RF red, audio orange, mark violet.
uint16_t fx_color(FxKind kind);
// alpha 0 gives `bg`, 1 gives `fg`, linear per channel.
uint16_t blend565(uint16_t fg, uint16_t bg, float alpha);

}  // namespace orcsdr::dead_air
