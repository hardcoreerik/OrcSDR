#pragma once
#include "state.hpp"
#include "ft8_control.hpp"
#include <cstdint>

namespace orc {
// Semantic input, independent of the Tab5 display geometry and ESP-NOW transport.
enum class ActionKind : uint8_t {
  none, tune, step, gain, squelch, volume, channel, radar_range,
  aircraft, lora_slot, node, event, p25_candidate, talkgroup,
  message, identity, wifi_ap, wifi_channel, setting, view, activate,
  span, filter,   // Home: zoom the spectrum, widen or narrow the receive filter (appended: values are on the wire)
  ft8_band, ft8_item, ft8_hunter,  // FT8 dashboard (appended after span/filter, so no existing value moves)
  ft8_fine                         // FT8 dashboard: centre tap toggles rotation between band selection and fine (Hz) tuning
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
  // Clockwise zooms in (a narrower span) and widens the filter.
  if (focus == Focus::span) return {ActionKind::span, detents};
  if (focus == Focus::filter) return {ActionKind::filter, detents};
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
    case Dashboard::ft8:
      if (view == static_cast<uint8_t>(ft8_control::View::live) ||
          view == static_cast<uint8_t>(ft8_control::View::hunter))
        return {ActionKind::ft8_band, detents};
      if (view == static_cast<uint8_t>(ft8_control::View::decodes) ||
          view == static_cast<uint8_t>(ft8_control::View::map) ||
          view == static_cast<uint8_t>(ft8_control::View::heard))
        return {ActionKind::ft8_item, detents};
      return {};
    default: return {};
  }
}
inline Action press(Dashboard id, uint8_t view, uint32_t capabilities = 0) {
  if (id == Dashboard::ft8) {
    if (view == static_cast<uint8_t>(ft8_control::View::live) ||
        view == static_cast<uint8_t>(ft8_control::View::hunter)) {
      const auto command = ft8_control::context_hunter_command(capabilities);
      return {ActionKind::ft8_hunter, static_cast<int32_t>(command)};
    }
    if (view == static_cast<uint8_t>(ft8_control::View::decodes) ||
        view == static_cast<uint8_t>(ft8_control::View::map) ||
        view == static_cast<uint8_t>(ft8_control::View::heard))
      return {ActionKind::activate, 1};
    return {};
  }
  if ((id == Dashboard::adsb && (view == 1 || view == 2)) ||
      (id == Dashboard::lora && (view == 1 || view == 2)) ||
      id == Dashboard::settings) return {ActionKind::activate, 1};
  return {ActionKind::view, 1};
}
inline Focus next_focus(Dashboard id, Focus focus) {
  if (id == Dashboard::home) {
    if (focus == Focus::vfo) return Focus::step;
    if (focus == Focus::step) return Focus::span;
    if (focus == Focus::span) return Focus::filter;
    if (focus == Focus::filter) return Focus::volume;
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
