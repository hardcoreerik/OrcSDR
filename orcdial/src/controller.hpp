#pragma once
#include "state.hpp"
#include <cstdint>

namespace orc {
// Semantic input, independent of the Tab5 display geometry and ESP-NOW transport.
enum class ActionKind : uint8_t {
  none, tune, step, gain, squelch, volume, channel, radar_range,
  aircraft, lora_slot, node, event, p25_candidate, talkgroup,
  message, identity, wifi_ap, wifi_channel, setting, view, activate
};
struct Action { ActionKind kind = ActionKind::none; int32_t value = 0; };
inline bool frequency_action(ActionKind kind) { return kind == ActionKind::tune; }
inline bool channel_dashboard(Dashboard id) {
  return id == Dashboard::weather || id == Dashboard::marine || id == Dashboard::cb;
}
inline Action rotate(Dashboard id, uint8_t view, Focus focus, int detents,
                     int acceleration, uint32_t step_hz) {
  if (!detents) return {};
  if (focus == Focus::volume) return {ActionKind::volume, detents};
  if (focus == Focus::gain) return {ActionKind::gain, detents};
  if (focus == Focus::squelch) return {ActionKind::squelch, detents};
  if (focus == Focus::step) return {ActionKind::step, detents};
  switch (id) {
    // Home tunes the whole range: the value is a count of steps, not Hz. The Tab5 applies the step of the
    // band it is in, so a spin that crosses a band edge changes step and snaps to the next band's raster.
    case Dashboard::home: return {ActionKind::tune, detents * acceleration};
    case Dashboard::fm: case Dashboard::am: case Dashboard::shortwave:
    case Dashboard::airband: case Dashboard::satellite: case Dashboard::rf_lab:
      { const int64_t hz = int64_t(detents) * acceleration * step_hz;
        return {ActionKind::tune, hz > 1000000000 ? 1000000000 :
                                  hz < -1000000000 ? -1000000000 : int32_t(hz)}; }
    case Dashboard::weather: case Dashboard::marine: case Dashboard::cb:
      return {ActionKind::channel, detents};
    case Dashboard::adsb:
      return {view == 0 ? ActionKind::radar_range :
              view == 1 || view == 2 ? ActionKind::aircraft : ActionKind::view, detents};
    case Dashboard::lora:
      return {view == 0 ? ActionKind::lora_slot :
              view == 1 || view == 3 ? ActionKind::node :
              view == 2 ? ActionKind::event : ActionKind::view, detents};
    case Dashboard::p25:
      return {view == 2 ? ActionKind::talkgroup : ActionKind::p25_candidate, detents};
    case Dashboard::pocsag:
      return {view == 1 ? ActionKind::identity : ActionKind::message, detents};
    case Dashboard::wifi_analysis:
      return {view == 1 ? ActionKind::wifi_channel : ActionKind::wifi_ap, detents};
    case Dashboard::settings: return {ActionKind::setting, detents};
    default: return {};
  }
}
inline Action press(Dashboard id, uint8_t view) {
  if ((id == Dashboard::adsb && (view == 1 || view == 2)) ||
      (id == Dashboard::lora && (view == 1 || view == 2)) ||
      id == Dashboard::settings) return {ActionKind::activate, 1};
  return {ActionKind::view, 1};
}
inline Focus next_focus(Dashboard id, Focus focus) {
  if (id == Dashboard::home) {
    if (focus == Focus::vfo) return Focus::step;
    if (focus == Focus::step) return Focus::volume;
    return Focus::vfo;
  }
  if (channel_dashboard(id)) {
    if (focus == Focus::vfo) return id == Dashboard::cb ? Focus::squelch : Focus::volume;
    return Focus::vfo;
  }
  if (id == Dashboard::fm) {
    if (focus == Focus::vfo) return Focus::step;
    if (focus == Focus::step) return Focus::volume;
    return Focus::vfo;
  }
  if (id == Dashboard::am || id == Dashboard::shortwave ||
      id == Dashboard::airband || id == Dashboard::satellite || id == Dashboard::rf_lab) {
    if (focus == Focus::vfo) return Focus::step;
    if (focus == Focus::step) return Focus::gain;
    if (focus == Focus::gain) return Focus::volume;
    return Focus::vfo;
  }
  return Focus::vfo;
}
} // namespace orc
