#include "receiver_tuning_controls.hpp"

#include <cstring>

namespace orcsdr::receiver_controls {
namespace {

bool tuner_path(const State& state) {
  return state.route != shortwave::ReceiverRoute::direct_q;
}

Item unavailable(const char* label, const State& state) {
  return {label,
          state.route == shortwave::ReceiverRoute::direct_q
              ? "Unavailable in Direct Q sampling"
              : "Not supported by this receiver",
          Availability::unavailable};
}

}  // namespace

Item item(Control control, const State& state) {
  switch (control) {
    case Control::rf_gain:
      return state.capabilities.rf_gain && tuner_path(state)
                 ? Item{"RF GAIN", "", Availability::enabled, !state.tuner_agc,
                        state.gain_tenth_db}
                 : unavailable("RF GAIN", state);
    case Control::tuner_agc:
      return state.capabilities.tuner_agc && tuner_path(state)
                 ? Item{"TUNER AGC", "", Availability::enabled, state.tuner_agc}
                 : unavailable("TUNER AGC", state);
    case Control::rtl_agc:
      return state.capabilities.rtl_agc
                 ? Item{"RTL AGC", "", Availability::enabled, state.rtl_agc}
                 : unavailable("RTL AGC", state);
    case Control::audio_boost:
      return {"AUDIO BOOST", "Post-demodulation audio gain", Availability::enabled,
              state.audio_boost};
    case Control::volume:
      return {"VOLUME", "Speaker output level", Availability::enabled, false,
              state.volume};
    case Control::offset_tuning:
      return state.capabilities.offset_tuning &&
                     state.route == shortwave::ReceiverRoute::tuner
                 ? Item{"OFFSET TUNING", "", Availability::enabled,
                        state.offset_tuning}
                 : Item{"OFFSET TUNING", "", Availability::hidden};
    case Control::count: break;
  }
  return {};
}

Action action(Control control, const State& state, int32_t value) {
  if (item(control, state).availability != Availability::enabled) return {};
  switch (control) {
    case Control::rf_gain: return {ActionKind::gain_tenth_db, value};
    case Control::tuner_agc: return {ActionKind::tuner_agc, !state.tuner_agc};
    case Control::rtl_agc: return {ActionKind::rtl_agc, !state.rtl_agc};
    case Control::audio_boost: return {ActionKind::audio_boost, !state.audio_boost};
    case Control::volume:
      return value >= 0 && value <= 255 ? Action{ActionKind::volume, value} : Action{};
    case Control::offset_tuning:
      return {ActionKind::offset_tuning, !state.offset_tuning};
    case Control::count: break;
  }
  return {};
}

bool self_check() {
  State v4{};
  v4.route = shortwave::ReceiverRoute::hf_upconverter;
  v4.capabilities = {true, true, true, false};
  v4.gain_tenth_db = 297;
  v4.volume = 80;
  const Item v4_gain = item(Control::rf_gain, v4);
  const Item v4_auto = item(Control::tuner_agc, v4);
  if (std::strcmp(v4_gain.label, "RF GAIN") != 0 ||
      v4_gain.availability != Availability::enabled || v4_gain.active ||
      v4_auto.availability != Availability::enabled || !v4_auto.active ||
      action(Control::tuner_agc, v4).kind != ActionKind::tuner_agc ||
      action(Control::tuner_agc, v4).value != 0) return false;

  State v3{};
  v3.route = shortwave::ReceiverRoute::direct_q;
  v3.capabilities = {true, true, false, true};
  return item(Control::rf_gain, v3).availability == Availability::unavailable &&
         item(Control::tuner_agc, v3).availability == Availability::unavailable &&
         item(Control::rtl_agc, v3).availability == Availability::unavailable &&
         item(Control::offset_tuning, v3).availability == Availability::hidden &&
         item(Control::audio_boost, v3).availability == Availability::enabled &&
         item(Control::volume, v3).availability == Availability::enabled &&
         action(Control::rf_gain, v3).kind == ActionKind::none &&
         action(Control::audio_boost, v3).kind == ActionKind::audio_boost;
}

}  // namespace orcsdr::receiver_controls
