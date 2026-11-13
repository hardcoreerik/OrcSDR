#include "serial_p25.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_p25 {

namespace {

// One formatted line, one write: what Serial.printf did for these replies (same 2048-byte limit).
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

void print_status(const Hooks& hooks) {
  const auto decoded = hooks.snapshot();
  unsigned grant_count = 0;
  for (const auto& grant : decoded.recent_grants) grant_count += grant.valid ? 1u : 0u;
  const Status status = hooks.status();
  const uint32_t encryption = status.encryption;
  const char* follow_name = status.follow == Follow::voice
                                ? "voice"
                                : status.follow == Follow::phase2_probe
                                      ? "phase2_probe"
                                      : "control";
  const p25config::Config& config = *hooks.config();
  say(hooks,
      "RTL_P25_STATUS configured=%d profile_id=\"%s\" profile=\"%s\" identity_source=air "
      "frequency_hz=%lu survey=%d candidate=%u hold=%d hold_tg=%u auto_follow=%d "
      "encryption_skip=%d relative_dbfs=%.1f "
      "modulation_configured=%s modulation_selected=%s lock_quality=%.1f "
      "timing_error=%.4f carrier_error_hz=%.1f decode_rate_hz=%.2f "
      "frame_sync=%d identity=%d nac=%03X wacn=%05lX sysid=%03X rfss=%u site=%u "
      "sync_words=%lu nid_good=%lu nid_failed=%lu tsbk_good=%lu tsbk_failed=%lu "
      "p2_band_plans=%lu p2_grants=%lu p2_mapping_errors=%lu p2_syncs=%lu "
      "p2_rejections=%lu p2_trace=%d "
      "p2_active=%d p2_symbols=%lu p2_sync_words=%lu p2_complete_bursts=%lu "
      "p2_truncated_bursts=%lu p2_duid_codeword=%02X p2_duid_valid=%d "
      "p2_duid=%u p2_duid_errors=%u p2_voice_bursts=%lu p2_control_bursts=%lu "
      "p2_unknown_bursts=%lu p2_reverse=%d "
      "p2_best_sync_errors=%u "
      "ber_percent=%.2f grants=%d grant_events=%lu follow=%s control_hz=%lu voice_hz=%lu "
      "voice_ldus=%lu voice_frames=%lu voice_queue_drops=%lu voice_unrouted=%lu "
      "enc_sync_good=%lu "
      "enc_sync_failed=%lu encrypted_detected=%d algid=%02X kid=%04X "
      "encrypted_muted_frames=%lu encrypted_returns=%lu imbe_frames=%lu "
      "imbe_errors=%lu pcm_frames=%lu voice_stack_hwm=%lu imbe_max_us=%lu "
      "imbe_synth_max_us=%lu audio_queue_max_us=%lu heap_free=%u heap_min=%u "
      "psram_free=%u usb_overruns=%u usb_drops=%u iq_drops=%u audio_drops=%u\n",
      config.control_channel_count > 0 ? 1 : 0, hooks.profiles()->active_id,
      config.system_name,
      static_cast<unsigned long>(status.frequency_hz),
      status.survey ? 1 : 0,
      static_cast<unsigned>(status.candidate),
      status.hold ? 1 : 0,
      static_cast<unsigned>(status.hold_talkgroup),
      status.auto_follow ? 1 : 0,
      status.encryption_skip ? 1 : 0,
      static_cast<double>(status.relative_dbfs),
      p25core::modulation_name(decoded.configured_modulation),
      p25core::modulation_name(decoded.selected_modulation),
      static_cast<double>(decoded.lock_quality_percent),
      static_cast<double>(decoded.timing_error),
      static_cast<double>(decoded.carrier_error_hz),
      static_cast<double>(decoded.decode_rate_hz),
      decoded.frame_sync ? 1 : 0, decoded.identity_valid ? 1 : 0,
      decoded.nac, static_cast<unsigned long>(decoded.wacn), decoded.system_id,
      decoded.rfss, decoded.site, static_cast<unsigned long>(decoded.sync_words),
      static_cast<unsigned long>(decoded.nid_good),
      static_cast<unsigned long>(decoded.nid_failed),
      static_cast<unsigned long>(decoded.tsbk_good),
      static_cast<unsigned long>(decoded.tsbk_failed),
      static_cast<unsigned long>(decoded.phase2_band_plans),
      static_cast<unsigned long>(status.phase2_grant_events),
      static_cast<unsigned long>(decoded.phase2_mapping_errors),
      static_cast<unsigned long>(status.phase2_sync_events),
      static_cast<unsigned long>(status.phase2_rejections),
      status.phase2_trace ? 1 : 0,
      decoded.phase2_acquisition ? 1 : 0,
      static_cast<unsigned long>(decoded.phase2_symbols),
      static_cast<unsigned long>(decoded.phase2_sync_words),
      static_cast<unsigned long>(decoded.phase2_complete_bursts),
      static_cast<unsigned long>(decoded.phase2_truncated_bursts),
      decoded.phase2_last_duid_codeword,
      decoded.phase2_last_duid_valid ? 1 : 0, decoded.phase2_last_duid,
      decoded.phase2_last_duid_errors,
      static_cast<unsigned long>(decoded.phase2_voice_bursts),
      static_cast<unsigned long>(decoded.phase2_control_bursts),
      static_cast<unsigned long>(decoded.phase2_unknown_bursts),
      decoded.phase2_reverse_polarity ? 1 : 0,
      decoded.phase2_best_sync_errors,
      static_cast<double>(decoded.estimated_ber_percent),
      grant_count,
      static_cast<unsigned long>(status.grant_events),
      follow_name,
      static_cast<unsigned long>(status.control_hz),
      static_cast<unsigned long>(status.voice_hz),
      static_cast<unsigned long>(decoded.voice_ldus),
      static_cast<unsigned long>(decoded.voice_frames),
      static_cast<unsigned long>(decoded.voice_queue_drops),
      static_cast<unsigned long>(decoded.voice_unrouted_frames),
      static_cast<unsigned long>(decoded.encryption_sync_good),
      static_cast<unsigned long>(decoded.encryption_sync_failed),
      status.encrypted_detected ? 1 : 0,
      static_cast<unsigned>(encryption >> 16),
      static_cast<unsigned>(encryption & 0xFFFFu),
      static_cast<unsigned long>(status.encrypted_muted_frames),
      static_cast<unsigned long>(status.encrypted_returns),
      static_cast<unsigned long>(status.imbe_frames),
      static_cast<unsigned long>(status.imbe_errors),
      static_cast<unsigned long>(status.pcm_frames),
      static_cast<unsigned long>(status.voice_stack_hwm),
      static_cast<unsigned long>(status.imbe_max_us),
      static_cast<unsigned long>(status.imbe_synth_max_us),
      static_cast<unsigned long>(status.audio_queue_max_us),
      static_cast<unsigned>(status.heap_free),
      static_cast<unsigned>(status.heap_min),
      static_cast<unsigned>(status.psram_free),
      static_cast<unsigned>(status.usb_overruns), static_cast<unsigned>(status.usb_drops),
      static_cast<unsigned>(status.iq_drops),
      static_cast<unsigned>(status.audio_drops));
  for (size_t i = 0; i < hooks.config()->control_channel_count; ++i)
    say(hooks, "RTL_P25_CANDIDATE index=%u frequency_hz=%lu relative_dbfs=%.1f\n",
        static_cast<unsigned>(i),
        static_cast<unsigned long>(hooks.config()->control_channels_hz[i]),
        static_cast<double>(hooks.candidate_level(i)));
}

void print_phase2_status(const Hooks& hooks) {
  const auto decoded = hooks.snapshot();
  const p25core::Grant* probed = hooks.phase2_probe_grant();
  const auto& grant = probed != nullptr ? *probed : decoded.current_grant;
  const Phase2Events events = hooks.phase2_events();
  say(hooks,
      "RTL_P25_PHASE2_STATUS trace=%d active=%d grants=%lu mapping_errors=%lu "
      "syncs=%lu rejections=%lu "
      "tg=%u src=%lu carrier_hz=%lu slot=%u channel_id=%u channel=%u service=%02X "
      "encrypted=%d wacn=%05lX sysid=%03X rfss=%u site=%u symbols=%lu "
      "sync_words=%lu complete_bursts=%lu truncated_bursts=%lu duid_codeword=%02X "
      "duid_valid=%d duid=%u duid_errors=%u voice_bursts=%lu control_bursts=%lu "
      "unknown_bursts=%lu reverse=%d best_sync_errors=%u last_sync_ms=%lu "
      "last_burst_ms=%lu\n",
      events.trace ? 1 : 0,
      decoded.phase2_acquisition ? 1 : 0,
      static_cast<unsigned long>(events.grants),
      static_cast<unsigned long>(decoded.phase2_mapping_errors),
      static_cast<unsigned long>(events.syncs),
      static_cast<unsigned long>(events.rejections),
      grant.talkgroup,
      static_cast<unsigned long>(grant.source_id),
      static_cast<unsigned long>(grant.frequency_hz), grant.slot, grant.channel_id,
      grant.channel_number, grant.service_options, grant.encrypted ? 1 : 0,
      static_cast<unsigned long>(grant.wacn), grant.system_id, grant.rfss, grant.site,
      static_cast<unsigned long>(decoded.phase2_symbols),
      static_cast<unsigned long>(decoded.phase2_sync_words),
      static_cast<unsigned long>(decoded.phase2_complete_bursts),
      static_cast<unsigned long>(decoded.phase2_truncated_bursts),
      decoded.phase2_last_duid_codeword,
      decoded.phase2_last_duid_valid ? 1 : 0, decoded.phase2_last_duid,
      decoded.phase2_last_duid_errors,
      static_cast<unsigned long>(decoded.phase2_voice_bursts),
      static_cast<unsigned long>(decoded.phase2_control_bursts),
      static_cast<unsigned long>(decoded.phase2_unknown_bursts),
      decoded.phase2_reverse_polarity ? 1 : 0,
      decoded.phase2_best_sync_errors,
      static_cast<unsigned long>(decoded.phase2_last_sync_ms),
      static_cast<unsigned long>(decoded.phase2_last_burst_ms));
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_P25_IQ_START") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_P25_IQ_ERROR auth_required");
      return true;
    }
    hooks.iq_start();
    return true;
  }
  if (strcmp(command, "RTL_P25_IQ_STOP") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_P25_IQ_ERROR auth_required");
      return true;
    }
    if (!hooks.iq_capture_is_p25()) {
      hooks.write_line("RTL_P25_IQ_ERROR no_p25_capture");
      return true;
    }
    hooks.iq_stop_and_export();
    return true;
  }
  if (strcmp(command, "RTL_P25_IQ_STATUS") == 0) {
    const IqStatus iq = hooks.iq_status();
    say(hooks,
        "RTL_P25_IQ_STATUS source=%s active=%d ready=%d bytes=%u max_bytes=%u "
        "frequency_hz=%lu rate=%u last_path=\"%s\"\n",
        iq.source,
        iq.active ? 1 : 0,
        iq.ready ? 1 : 0,
        static_cast<unsigned>(iq.bytes),
        static_cast<unsigned>(iq.max_bytes),
        static_cast<unsigned long>(iq.frequency_hz), static_cast<unsigned>(iq.rate),
        iq.last_path[0] ? iq.last_path : "none");
    return true;
  }
  if (strncmp(command, "RTL_P25_REPLAY ", 15) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_P25_REPLAY_ERROR auth_required");
      return true;
    }
    hooks.replay(command + 15);
    return true;
  }
  if (strcmp(command, "RTL_P25_ENCRYPTION_STATUS") == 0) {
    const Encryption state = hooks.encryption();
    const uint32_t encryption = state.last;
    say(hooks,
        "RTL_P25_ENCRYPTION_STATUS detected=%d algid=%02X kid=%04X muted_frames=%lu "
        "returns=%lu skip_enabled=%d\n",
        state.detected ? 1 : 0,
        static_cast<unsigned>(encryption >> 16),
        static_cast<unsigned>(encryption & 0xFFFFu),
        static_cast<unsigned long>(state.muted_frames),
        static_cast<unsigned long>(state.returns),
        state.skip_enabled ? 1 : 0);
    return true;
  }
  if (strcmp(command, "RTL_P25_MODULATION") == 0) {
    const auto decoded = hooks.snapshot();
    const p25config::Config& config = *hooks.config();
    say(hooks, "RTL_P25_MODULATION configured=%s selected=%s timing_gain=%.6f carrier_gain=%.6f\n",
        p25core::modulation_name(config.modulation),
        p25core::modulation_name(decoded.selected_modulation),
        static_cast<double>(config.cqpsk_timing_gain),
        static_cast<double>(config.cqpsk_carrier_gain));
    return true;
  }
  if (strncmp(command, "RTL_P25_MODULATION ", 19) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_P25_MODULATION_ERROR auth_required");
      return true;
    }
    const char* value = command + 19;
    p25core::Modulation modulation = p25core::Modulation::auto_detect;
    if (strcmp(value, "AUTO") == 0) modulation = p25core::Modulation::auto_detect;
    else if (strcmp(value, "C4FM") == 0) modulation = p25core::Modulation::c4fm;
    else if (strcmp(value, "CQPSK") == 0) modulation = p25core::Modulation::cqpsk;
    else {
      hooks.write_line("RTL_P25_MODULATION_ERROR usage: RTL_P25_MODULATION AUTO|C4FM|CQPSK");
      return true;
    }
    hooks.set_modulation(modulation);
    say(hooks, "RTL_P25_MODULATION_OK configured=%s\n",
        p25core::modulation_name(hooks.config()->modulation));
    return true;
  }
  if (strcmp(command, "RTL_P25_STATUS") == 0) {
    print_status(hooks);
    return true;
  }
  if (strcmp(command, "RTL_P25_PHASE2_STATUS") == 0) {
    print_phase2_status(hooks);
    return true;
  }
  if (strcmp(command, "RTL_P25_PHASE2_TRACE") == 0) {
    say(hooks, "RTL_P25_PHASE2_TRACE enabled=%d\n",
        hooks.phase2_trace() ? 1 : 0);
    return true;
  }
  if (strncmp(command, "RTL_P25_PHASE2_TRACE ", 21) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_P25_PHASE2_TRACE_ERROR auth_required");
      return true;
    }
    const char* value = command + 21;
    if (strcmp(value, "ON") != 0 && strcmp(value, "OFF") != 0) {
      hooks.write_line("RTL_P25_PHASE2_TRACE_ERROR usage: RTL_P25_PHASE2_TRACE ON|OFF");
      return true;
    }
    const bool enabled = strcmp(value, "ON") == 0;
    hooks.set_phase2_trace(enabled);
    say(hooks, "RTL_P25_PHASE2_TRACE_OK enabled=%d\n", enabled ? 1 : 0);
    return true;
  }
  if (strcmp(command, "RTL_P25_PROFILE_LIST") == 0) {
    const p25config::StoreState& profiles = *hooks.profiles();
    say(hooks, "RTL_P25_PROFILE_LIST_BEGIN count=%u active=\"%s\"\n",
        static_cast<unsigned>(profiles.count), profiles.active_id);
    for (size_t i = 0; i < profiles.count; ++i)
      say(hooks, "RTL_P25_PROFILE index=%u id=\"%s\" name=\"%s\" active=%d\n",
          static_cast<unsigned>(i), profiles.profiles[i].id,
          profiles.profiles[i].name,
          static_cast<int>(i) == profiles.active_index ? 1 : 0);
    hooks.write_line("RTL_P25_PROFILE_LIST_DONE");
    return true;
  }
  if (strncmp(command, "RTL_P25_PROFILE_SELECT ", 23) == 0) {
    if (!authenticated) { hooks.write_line("RTL_P25_PROFILE_ERROR auth_required"); return true; }
    char error[64]{};
    const char* id = command + 23;
    if (!hooks.profile_select(id, error, sizeof(error))) {
      say(hooks, "RTL_P25_PROFILE_ERROR operation=select detail=\"%s\"\n", error);
      return true;
    }
    say(hooks, "RTL_P25_PROFILE_OK operation=select id=\"%s\"\n", id);
    return true;
  }
  if (strncmp(command, "RTL_P25_PROFILE_IMPORT ", 23) == 0) {
    if (!authenticated) { hooks.write_line("RTL_P25_PROFILE_ERROR auth_required"); return true; }
    char path[96]{}, id[32]{}, error[64]{};
    if (sscanf(command + 23, "%95s %31s", path, id) != 2 ||
        !hooks.profile_import(path, id, error, sizeof(error))) {
      say(hooks, "RTL_P25_PROFILE_ERROR operation=import detail=\"%s\"\n",
          error[0] ? error : "usage");
      return true;
    }
    hooks.load_config();
    say(hooks, "RTL_P25_PROFILE_OK operation=import id=\"%s\" source=\"%s\"\n", id, path);
    return true;
  }
  if (strncmp(command, "RTL_P25_PROFILE_EXPORT ", 23) == 0) {
    if (!authenticated) { hooks.write_line("RTL_P25_PROFILE_ERROR auth_required"); return true; }
    char id[32]{}, path[96]{}, error[64]{};
    if (sscanf(command + 23, "%31s %95s", id, path) != 2 ||
        !hooks.profile_export(id, path, error, sizeof(error))) {
      say(hooks, "RTL_P25_PROFILE_ERROR operation=export detail=\"%s\"\n",
          error[0] ? error : "usage");
      return true;
    }
    say(hooks, "RTL_P25_PROFILE_OK operation=export id=\"%s\" path=\"%s\"\n", id, path);
    return true;
  }
  if (strncmp(command, "RTL_P25_PROFILE_RENAME ", 23) == 0) {
    if (!authenticated) { hooks.write_line("RTL_P25_PROFILE_ERROR auth_required"); return true; }
    char id[32]{}, name[48]{}, error[64]{};
    const int parsed = sscanf(command + 23, "%31s %47[^\n]", id, name);
    if (parsed != 2 ||
        !hooks.profile_rename(id, name, error, sizeof(error))) {
      say(hooks, "RTL_P25_PROFILE_ERROR operation=rename detail=\"%s\"\n",
          error[0] ? error : "usage");
      return true;
    }
    if (strcmp(id, hooks.profiles()->active_id) == 0) hooks.load_config();
    say(hooks, "RTL_P25_PROFILE_OK operation=rename id=\"%s\" name=\"%s\"\n", id, name);
    return true;
  }
  if (strncmp(command, "RTL_P25_PROFILE_DELETE ", 23) == 0) {
    if (!authenticated) { hooks.write_line("RTL_P25_PROFILE_ERROR auth_required"); return true; }
    char id[32]{}, confirm[16]{}, error[64]{};
    if (sscanf(command + 23, "%31s %15s", id, confirm) != 2 ||
        strcmp(confirm, "CONFIRM") != 0 ||
        !hooks.profile_delete(id, error, sizeof(error))) {
      say(hooks, "RTL_P25_PROFILE_ERROR operation=delete detail=\"%s\"\n",
          error[0] ? error : "confirmation_required");
      return true;
    }
    hooks.load_config();
    say(hooks, "RTL_P25_PROFILE_OK operation=delete id=\"%s\"\n", id);
    return true;
  }
  if (strcmp(command, "RTL_P25_CONFIG_RELOAD") == 0 && authenticated) {
    say(hooks, "RTL_P25_CONFIG_RELOAD status=\"%s\"\n", hooks.config_reload());
    return true;
  }
  if (strcmp(command, "RTL_P25_SCAN") == 0 && authenticated) {
    hooks.scan();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_p25
