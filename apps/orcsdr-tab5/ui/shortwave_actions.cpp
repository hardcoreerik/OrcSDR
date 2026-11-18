#include "shortwave_actions.hpp"

#include "shortwave_audio_dsp.hpp"
#include "shortwave_ssb.hpp"

#include <algorithm>

namespace orcsdr::shortwave_actions {
namespace {

using orcsdr::action_io::say;
using orcsdr::shortwave::ActionKind;

void tune(const Hooks& hooks, uint32_t hz) {
  const uint32_t frequency = hooks.clamp_frequency(hz);
  hooks.note_tuned(frequency);
  if (hooks.capture_running())
    hooks.request_hot_retune(frequency);
  else
    hooks.queue_local_listen(frequency);
}

void set_filter_bandwidth(const Hooks& hooks, uint32_t hz) {
  hooks.filter_bandwidth_hz->store(hz, std::memory_order_relaxed);
  hooks.audio_reset_demod_filters();
  hooks.reset_spectrum_renderer();
}

}  // namespace

void handle(const orcsdr::shortwave::Action& action, const Hooks& hooks) {
  switch (action.kind) {
    case ActionKind::tune_hz: tune(hooks, static_cast<uint32_t>(action.value)); break;
    case ActionKind::open_frequency:
      return;  // the Shortwave dashboard opens its own keypad
    case ActionKind::step_down:
      tune(hooks, hooks.step_frequency(-1));
      break;
    case ActionKind::step_up:
      tune(hooks, hooks.step_frequency(1));
      break;
    case ActionKind::step_cycle:
      *hooks.shortwave_step_hz = hooks.next_tuning_step(*hooks.shortwave_step_hz);
      break;
    case ActionKind::filter_cycle: {
      const uint32_t current = hooks.filter_bandwidth_hz->load(std::memory_order_relaxed);
      const uint32_t next = current <= 4000u ? 6000u : current <= 6000u ? 9000u : 4000u;
      set_filter_bandwidth(hooks, next);
      break;
    }
    case ActionKind::filter_down:
    case ActionKind::filter_up: {
      const uint32_t current = hooks.filter_bandwidth_hz->load(std::memory_order_relaxed);
      const uint32_t next = action.kind == ActionKind::filter_down
                                ? (current <= 6000u ? 4000u : 6000u)
                                : (current < 6000u ? 6000u : 9000u);
      set_filter_bandwidth(hooks, next);
      break;
    }
    case ActionKind::filter_bandwidth_hz:
      set_filter_bandwidth(hooks, hooks.clamp_filter_hz(static_cast<uint32_t>(action.value)));
      break;
    case ActionKind::span_down:
    case ActionKind::span_up: {
      const uint32_t current = hooks.scope_span_hz->load(std::memory_order_relaxed);
      hooks.scope_span_hz->store(action.kind == ActionKind::span_down
                                     ? std::max(hooks.scope_span_min_hz, current / 2)
                                     : std::min(hooks.span_cap_hz(), current * 2),
                                 std::memory_order_relaxed);
      hooks.reset_spectrum_renderer();
      break;
    }
    case ActionKind::span_hz:
      hooks.scope_span_hz->store(
          std::clamp(static_cast<uint32_t>(action.value), hooks.scope_span_min_hz, hooks.span_cap_hz()),
          std::memory_order_relaxed);
      hooks.reset_spectrum_renderer();
      break;
    case ActionKind::sound_toggle:
      hooks.set_audio_user_enabled(!hooks.audio_user_enabled());
      break;
    case ActionKind::volume_down: hooks.adjust_volume(-hooks.volume_step); break;
    case ActionKind::volume_up: hooks.adjust_volume(hooks.volume_step); break;
    case ActionKind::gain_auto:
      if (hooks.tuner_gain_ready()) {
        const bool next_auto = !hooks.tuner_gain_is_auto();
        const char* result = hooks.set_tuner_gain_mode(next_auto);
        say(hooks.io, "RTL_SHORTWAVE_TUNER_AGC enabled=%d result=%s\n", next_auto, result);
      }
      break;
    case ActionKind::gain_tenth_db:
      if (hooks.tuner_gain_ready()) {
        hooks.set_tuner_gain_manual();
        say(hooks.io, "RTL_SHORTWAVE_RF_GAIN gain_tenth_db=%ld result=%s\n",
            static_cast<long>(action.value), hooks.set_tuner_gain(static_cast<int>(action.value)));
      }
      break;
    case ActionKind::rtl_agc:
      if (hooks.rtl_agc_available())
        say(hooks.io, "RTL_SHORTWAVE_RTL_AGC enabled=%ld result=%s\n",
            static_cast<long>(action.value), hooks.set_rtl_agc(action.value != 0));
      break;
    case ActionKind::mode_cycle: {
      namespace ssb = orcsdr::shortwave_ssb;
      const auto next = ssb::next_choice(static_cast<ssb::Choice>(ssb::control().choice.load(std::memory_order_relaxed)));
      ssb::control().choice.store(static_cast<uint8_t>(next), std::memory_order_relaxed);
      say(hooks.io, "RTL_SHORTWAVE_MODE choice=%u mode=%s\n", static_cast<unsigned>(next),
          ssb::mode_name(ssb::current(*hooks.ui_frequency_hz)));
      break;
    }
    case ActionKind::clarifier_down:
    case ActionKind::clarifier_up: {
      namespace ssb = orcsdr::shortwave_ssb;
      const int32_t step = action.kind == ActionKind::clarifier_up ? ssb::kClarifierStepHz : -ssb::kClarifierStepHz;
      ssb::control().clarifier_hz.store(
          ssb::clamp_clarifier(ssb::control().clarifier_hz.load(std::memory_order_relaxed) + step),
          std::memory_order_relaxed);
      break;
    }
    case ActionKind::audio_boost:
      hooks.audio_boost->store(action.value != 0, std::memory_order_release);
      say(hooks.io, "RTL_SHORTWAVE_AUDIO_BOOST enabled=%ld gain_db=12\n",
          static_cast<long>(action.value));
      break;
    case ActionKind::clean_audio:
      hooks.filter_bandwidth_hz->store(6000, std::memory_order_relaxed);
      hooks.audio_boost->store(false, std::memory_order_release);
      orcsdr::shortwave::audio_dsp::apply_clean_preset();
      orcsdr::shortwave::audio_dsp::reset();
      hooks.audio_reset_demod_filters();
      hooks.reset_spectrum_renderer();
      hooks.io.write_line("RTL_SHORTWAVE_DSP clean=1 bandwidth_hz=6000 nr=low notch=auto sql=off");
      break;
    case ActionKind::noise_reduction_cycle:
      orcsdr::shortwave::audio_dsp::cycle_noise_reduction();
      say(hooks.io, "RTL_SHORTWAVE_DSP nr=%u\n", static_cast<unsigned>(
          orcsdr::shortwave::audio_dsp::settings().noise_reduction));
      break;
    case ActionKind::auto_notch_toggle:
      orcsdr::shortwave::audio_dsp::toggle_auto_notch();
      say(hooks.io, "RTL_SHORTWAVE_DSP notch=%d\n",
          orcsdr::shortwave::audio_dsp::settings().auto_notch);
      break;
    case ActionKind::squelch_cycle:
      orcsdr::shortwave::audio_dsp::cycle_squelch();
      say(hooks.io, "RTL_SHORTWAVE_DSP squelch=%u\n", static_cast<unsigned>(
          orcsdr::shortwave::audio_dsp::settings().squelch));
      break;
    case ActionKind::squelch_down:
      orcsdr::shortwave::audio_dsp::adjust_squelch(-5);
      break;
    case ActionKind::squelch_up:
      orcsdr::shortwave::audio_dsp::adjust_squelch(5);
      break;
    case ActionKind::hunt_start:
      if (action.value >= 0 && static_cast<size_t>(action.value) < hooks.hunt_band_count()) {
        hooks.hunt_band_index->store(static_cast<uint8_t>(action.value), std::memory_order_release);
        hooks.hunt_requested->store(true, std::memory_order_release);
      }
      break;
    case ActionKind::hunt_cancel:
      hooks.hunt_requested->store(false, std::memory_order_release);
      hooks.hunt_cancel_requested->store(true, std::memory_order_release);
      break;
    case ActionKind::save_memory:
    case ActionKind::update_memory:
    case ActionKind::favorite_memory:
    case ActionKind::delete_memory:
    case ActionKind::save_log:
    case ActionKind::update_log:
    case ActionKind::delete_log:
    case ActionKind::export_log:
      orcsdr::shortwave_library_actions::handle(action, hooks.library);
      break;
    case ActionKind::open_settings:
      hooks.open_radio_defaults_settings();
      return;
    case ActionKind::exit_home:
      hooks.hunt_requested->store(false, std::memory_order_release);
      hooks.hunt_cancel_requested->store(true, std::memory_order_release);
      hooks.leave_dashboard();
      hooks.show_home();
      return;
    case ActionKind::none: break;
  }
  hooks.refresh_active_screen();
}

}  // namespace orcsdr::shortwave_actions
