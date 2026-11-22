#include "p25_actions.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::p25_actions {
namespace {

using orcsdr::action_io::copy_text;
using orcsdr::action_io::say;
using orcsdr::p25::ActionKind;

// Back to the control channel after a followed voice call.
void leave_voice_follow(const Hooks& hooks) {
  hooks.follow_to_control();
  hooks.suspend_voice();
  hooks.voice_session->fetch_add(1, std::memory_order_acq_rel);
  *hooks.voice_frequency_hz = 0;
  hooks.request_hot_retune(*hooks.control_frequency_hz);
}

void step_candidate(const Hooks& hooks, int step) {
  const orcsdr::p25config::Config& config = *hooks.config;
  if (config.control_channel_count == 0) return;
  hooks.cancel_survey();
  *hooks.candidate_index = static_cast<uint8_t>(
      step < 0 ? (*hooks.candidate_index + config.control_channel_count - 1) % config.control_channel_count
               : (*hooks.candidate_index + 1) % config.control_channel_count);
  hooks.tune_control(config.control_channels_hz[*hooks.candidate_index]);
  hooks.request_config_save();
}

void retune_after_reload(const Hooks& hooks) {
  if (hooks.band_is_p25() && hooks.config->control_channel_count > 0)
    hooks.tune_control(*hooks.control_frequency_hz);
}

void set_status(const Hooks& hooks, const char* prefix, const char* error) {
  snprintf(hooks.config_status, hooks.config_status_size, "%s: %.45s", prefix, error);
}

}  // namespace

void handle(const orcsdr::p25::Action& action, const Hooks& hooks) {
  auto& profiles = *hooks.profiles;
  switch (action.kind) {
    case ActionKind::tune_hz:
      hooks.cancel_survey();
      hooks.tune_control(action.value);
      break;
    case ActionKind::previous_candidate:
      step_candidate(hooks, -1);
      break;
    case ActionKind::next_candidate:
      step_candidate(hooks, 1);
      break;
    case ActionKind::survey_toggle:
      if (hooks.survey_active->load(std::memory_order_relaxed)) {
        hooks.survey_cancel_requested->store(true, std::memory_order_release);
      } else {
        hooks.start_survey();
      }
      break;
    case ActionKind::hold_toggle:
      if (hooks.hold->load(std::memory_order_relaxed)) {
        hooks.hold->store(false, std::memory_order_relaxed);
        *hooks.hold_talkgroup = 0;
      } else {
        *hooks.hold_talkgroup = hooks.grant_talkgroup_to_hold();
        hooks.hold->store(true, std::memory_order_relaxed);
      }
      say(hooks.io, "RTL_P25_HOLD enabled=%d tg=%u mode=%s\n",
          hooks.hold->load(std::memory_order_relaxed) ? 1 : 0,
          static_cast<unsigned>(*hooks.hold_talkgroup), *hooks.hold_talkgroup ? "talkgroup" : "next");
      hooks.request_config_save();
      break;
    case ActionKind::hold_talkgroup:
      if (hooks.hold->load(std::memory_order_relaxed) && *hooks.hold_talkgroup == action.value) {
        hooks.hold->store(false, std::memory_order_relaxed);
        *hooks.hold_talkgroup = 0;
      } else {
        *hooks.hold_talkgroup = static_cast<uint16_t>(action.value);
        hooks.hold->store(true, std::memory_order_relaxed);
      }
      say(hooks.io, "RTL_P25_HOLD enabled=%d tg=%u\n",
          hooks.hold->load(std::memory_order_relaxed) ? 1 : 0,
          static_cast<unsigned>(*hooks.hold_talkgroup));
      hooks.request_config_save();
      break;
    case ActionKind::skip_talkgroup: {
      const uint16_t talkgroup = hooks.current_talkgroup();
      if (talkgroup != 0) {
        *hooks.skipped_talkgroup = talkgroup;
        *hooks.skip_until_ms = hooks.millis_now() + 30000;
        say(hooks.io, "RTL_P25_SKIP tg=%u duration_ms=30000\n", static_cast<unsigned>(talkgroup));
      } else {
        // Nothing to remember yet (the call's talkgroup is not decoded): the call is still dropped, but the same
        // talkgroup can pull the receiver straight back. Say so, so a "skip did nothing" report can be explained.
        say(hooks.io, "RTL_P25_SKIP tg=unknown remembered=0 left_call=%d\n", hooks.follow_is_voice() ? 1 : 0);
      }
      if (hooks.follow_is_voice()) leave_voice_follow(hooks);
      break;
    }
    case ActionKind::auto_follow_toggle:
      hooks.auto_follow->store(!hooks.auto_follow->load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
      if (!hooks.auto_follow->load(std::memory_order_relaxed) && hooks.follow_is_voice())
        leave_voice_follow(hooks);
      hooks.request_config_save();
      break;
    case ActionKind::encryption_skip_toggle:
      hooks.encryption_skip->store(!hooks.encryption_skip->load(std::memory_order_relaxed),
                                   std::memory_order_relaxed);
      hooks.request_config_save();
      break;
    case ActionKind::reload_config:
      hooks.cancel_survey();
      hooks.load_config();
      retune_after_reload(hooks);
      break;
    case ActionKind::select_profile: {
      if (!hooks.sd_available() || action.value >= profiles.count) break;
      char error[64]{};
      if (hooks.select_profile(profiles.profiles[action.value].id, error, sizeof(error))) {
        retune_after_reload(hooks);
      } else {
        set_status(hooks, "Select failed", error);
      }
      break;
    }
    case ActionKind::import_profile: {
      if (!hooks.sd_ready()) break;
      char id[32] = "imported";
      for (unsigned suffix = 1; suffix < 100; ++suffix) {
        bool used = false;
        for (size_t i = 0; i < profiles.count; ++i)
          used |= strcmp(profiles.profiles[i].id, id) == 0;
        if (!used) break;
        snprintf(id, sizeof(id), "imported-%u", suffix);
      }
      char error[64]{};
      if (hooks.import_profile(id, error, sizeof(error))) {
        hooks.load_config();
      } else {
        set_status(hooks, "Import failed", error);
      }
      break;
    }
    case ActionKind::export_profile: {
      if (!hooks.sd_available() || action.value >= profiles.count) break;
      char error[64]{};
      const bool ok = hooks.export_profile(profiles.profiles[action.value].id, error, sizeof(error));
      copy_text(hooks.config_status, ok ? "Exported /orcsdr/exports/p25-profile.cfg" : error,
                hooks.config_status_size);
      break;
    }
    case ActionKind::rename_profile: {
      if (!hooks.sd_available() || action.value >= profiles.count) break;
      char name[48]{}, error[64]{};
      if (hooks.take_profile_name(name, sizeof(name)) &&
          hooks.rename_profile(profiles.profiles[action.value].id, name, error, sizeof(error)))
        hooks.load_config();
      else if (error[0])
        set_status(hooks, "Rename failed", error);
      break;
    }
    case ActionKind::delete_profile: {
      if (!hooks.sd_available() || action.value >= profiles.count) break;
      char id[32]{}, error[64]{};
      copy_text(id, profiles.profiles[action.value].id, sizeof(id));
      if (hooks.delete_profile(id, error, sizeof(error)))
        hooks.load_config();
      else
        set_status(hooks, "Delete failed", error);
      break;
    }
    case ActionKind::span_down:
    case ActionKind::span_up: {
      const uint32_t current = hooks.scope_span_hz->load(std::memory_order_relaxed);
      hooks.scope_span_hz->store(
          action.kind == ActionKind::span_down
              ? std::max(hooks.scope_span_min_hz, current / 2)
              : std::min(hooks.span_cap_hz(), current * 2),
          std::memory_order_relaxed);
      hooks.reset_spectrum_renderer();
      break;
    }
    case ActionKind::sound_toggle:
      hooks.set_audio_user_enabled(!hooks.audio_user_enabled());
      break;
    case ActionKind::volume_down:
      hooks.adjust_volume(-hooks.volume_step);
      break;
    case ActionKind::volume_up:
      hooks.adjust_volume(hooks.volume_step);
      break;
    case ActionKind::open_device_settings:
      hooks.open_radio_defaults_settings();
      break;
    case ActionKind::exit_to_home:
      hooks.leave_dashboard();
      hooks.show_home();
      break;
    case ActionKind::none: break;
  }
  hooks.refresh_active_screen();
}

}  // namespace orcsdr::p25_actions
