#include "lora_diag_commands.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lora_channel_control.hpp"
#include "lora_native_decoder.hpp"
#include "lora_preamble_detector.hpp"

namespace orcsdr::lora_diag {
namespace {

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void emit(const Hooks& hooks, const char* format, ...) {
  if (hooks.write_line == nullptr) return;
  char line[704];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line) - 1, format, args);
  va_end(args);
  line[sizeof(line) - 1] = '\0';
  // The console prints exactly what it is given; terminate the line here.
  const size_t length = strlen(line);
  if (length + 1 < sizeof(line)) {
    line[length] = '\n';
    line[length + 1] = '\0';
  }
  hooks.write_line(line);
}

bool denied(const Hooks& hooks, bool authenticated, const char* error_prefix) {
  if (authenticated) return false;
  emit(hooks, "%s_ERROR auth_required", error_prefix);
  return true;
}

void detector_status(const Hooks& hooks) {
  DetectorState s{};
  if (hooks.detector_state != nullptr) hooks.detector_state(&s);
  const auto pd = lora_preamble::status();
  emit(hooks,
       "RTL_LORA_DETECTOR_STATUS enabled=%s armed=%d level_dbfs=%.1f noise_dbfs=%.1f "
       "trigger_dbfs=%.1f margin_db=%.1f hysteresis_db=%.1f ceiling_dbfs=%.1f "
       "sustained_ms=%u relearns=%lu events=%lu drops=%lu capture_active=%d "
       "capture_ready=%d decode_busy=%d mode=%s pd_ready=%d pd_windows=%lu "
       "pd_fires=%lu pd_last_bin=%u pd_last_ratio=%.1f pd_run=%u pd_max_window_us=%lu",
       s.enabled ? "on" : "off", s.armed ? 1 : 0, static_cast<double>(s.level_dbfs),
       static_cast<double>(s.noise_dbfs), static_cast<double>(s.trigger_dbfs),
       static_cast<double>(s.margin_db), static_cast<double>(s.hysteresis_db),
       static_cast<double>(s.ceiling_dbfs), static_cast<unsigned>(s.sustained_ms),
       static_cast<unsigned long>(s.relearns), static_cast<unsigned long>(s.events),
       static_cast<unsigned long>(s.drops), s.capture_active ? 1 : 0, s.capture_ready ? 1 : 0,
       s.decode_busy ? 1 : 0, s.preamble_mode ? "preamble" : "energy",
       lora_preamble::ready() ? 1 : 0, static_cast<unsigned long>(pd.windows),
       static_cast<unsigned long>(pd.fires), static_cast<unsigned>(pd.last_bin),
       static_cast<double>(pd.last_ratio), static_cast<unsigned>(pd.run),
       static_cast<unsigned long>(pd.max_window_us));
}

bool handle_detector(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_LORA_DETECTOR STATUS") == 0) {
    detector_status(hooks);
    return true;
  }
  if (strcmp(command, "RTL_LORA_DETECTOR RELEARN") == 0) {
    if (denied(hooks, authenticated, "RTL_LORA_DETECTOR")) return true;
    if (hooks.request_relearn == nullptr) {
      emit(hooks, "RTL_LORA_DETECTOR_ERROR relearn=unavailable");
      return true;
    }
    hooks.request_relearn();
    emit(hooks, "RTL_LORA_DETECTOR_OK relearn=queued");
    return true;
  }
  static constexpr char kMode[] = "RTL_LORA_DETECTOR MODE ";
  if (strncmp(command, kMode, sizeof(kMode) - 1) == 0) {
    if (denied(hooks, authenticated, "RTL_LORA_DETECTOR")) return true;
    const char* value = command + sizeof(kMode) - 1;
    const bool preamble = strcmp(value, "PREAMBLE") == 0;
    if (!preamble && strcmp(value, "ENERGY") != 0) {
      emit(hooks, "RTL_LORA_DETECTOR_ERROR usage=RTL_LORA_DETECTOR MODE PREAMBLE|ENERGY");
      return true;
    }
    if (hooks.set_preamble_mode == nullptr) {
      emit(hooks, "RTL_LORA_DETECTOR_ERROR mode=unavailable");
      return true;
    }
    hooks.set_preamble_mode(preamble);
    emit(hooks, "RTL_LORA_DETECTOR_OK mode=%s", preamble ? "preamble" : "energy");
    return true;
  }
  return false;
}

bool handle_decoder(const char* command, bool authenticated, const Hooks& hooks) {
  static constexpr char kPrefix[] = "RTL_LORA_DECODER ";
  if (strncmp(command, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
  const char* args = command + sizeof(kPrefix) - 1;
  if (strcmp(args, "STATUS") == 0) {
    emit(hooks, "RTL_LORA_DECODER_STATUS search_padding=%u",
         static_cast<unsigned>(lora_native::search_padding()));
    return true;
  }
  if (denied(hooks, authenticated, "RTL_LORA_DECODER")) return true;
  int bias = 0;
  if (sscanf(args, "TIMING_BIAS %d", &bias) == 1) {
    lora_native::set_timing_bias(bias);
    emit(hooks, "RTL_LORA_DECODER_OK timing_bias=%d", lora_native::timing_bias());
    return true;
  }
  unsigned padding = 0;
  if (sscanf(args, "SEARCH_PAD %u", &padding) != 1 || (padding != 1 && padding != 4)) {
    emit(hooks, "RTL_LORA_DECODER_ERROR usage=RTL_LORA_DECODER STATUS|SEARCH_PAD <1|4>|TIMING_BIAS <n>");
    return true;
  }
  lora_native::set_search_padding(padding);
  emit(hooks, "RTL_LORA_DECODER_OK search_padding=%u",
       static_cast<unsigned>(lora_native::search_padding()));
  return true;
}

bool handle_stats(const char* command, bool authenticated, const Hooks& hooks) {
  const bool reset = strcmp(command, "RTL_LORA_STATS RESET") == 0;
  if (!reset && strcmp(command, "RTL_LORA_STATS") != 0) return false;
  if (reset) {
    if (denied(hooks, authenticated, "RTL_LORA_STATS")) return true;
    if (hooks.reset_counters == nullptr) {
      emit(hooks, "RTL_LORA_STATS_ERROR reset=unavailable");
      return true;
    }
    hooks.reset_counters();
    emit(hooks, "RTL_LORA_STATS_OK reset=1");
    return true;
  }
  Counters c{};
  if (hooks.counters != nullptr) hooks.counters(&c);
  emit(hooks,
       "RTL_LORA_STATS events=%lu crc_ok=%lu encrypted=%lu messages=%lu failures=%lu "
       "drops=%lu relearns=%lu last_decode_ms=%lu",
       static_cast<unsigned long>(c.events), static_cast<unsigned long>(c.crc_ok),
       static_cast<unsigned long>(c.encrypted), static_cast<unsigned long>(c.messages),
       static_cast<unsigned long>(c.failures), static_cast<unsigned long>(c.drops),
       static_cast<unsigned long>(c.relearns), static_cast<unsigned long>(c.last_decode_ms));
  return true;
}

bool handle_plan(const char* command, bool authenticated, const Hooks& hooks) {
  static constexpr char kPrefix[] = "RTL_LORA_PLAN SET ";
  if (strncmp(command, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
  if (denied(hooks, authenticated, "RTL_LORA_PLAN")) return true;
  char region_text[24]{};
  unsigned slot = 0;
  const int fields = sscanf(command + sizeof(kPrefix) - 1, "%23s %u", region_text, &slot);
  int region_index = fields >= 1 ? lora_channel::find_region(region_text) : -1;
  if (region_index < 0 && fields >= 1) {
    char* end = nullptr;
    const unsigned long numeric = strtoul(region_text, &end, 10);
    if (end != region_text && *end == '\0' && numeric >= 1 &&
        numeric <= lora_channel::region_count())
      region_index = static_cast<int>(numeric - 1);
  }
  ChannelSelection chosen{};
  if (region_index < 0 || slot > 0xFFFFu || hooks.select_channel == nullptr ||
      !hooks.select_channel(static_cast<size_t>(region_index), static_cast<uint16_t>(slot),
                            &chosen)) {
    emit(hooks, "RTL_LORA_PLAN_ERROR usage=RTL_LORA_PLAN SET <region code|index> [slot]");
    return true;
  }
  emit(hooks, "RTL_LORA_PLAN_OK region=%s slot=%u frequency_hz=%u", chosen.region,
       static_cast<unsigned>(chosen.slot), static_cast<unsigned>(chosen.frequency_hz));
  return true;
}

bool handle_recent(const char* command, const Hooks& hooks) {
  if (strcmp(command, "RTL_LORA_RECENT") != 0) return false;
  RecentPacket packets[kMaxRecentPackets];
  const size_t count =
      hooks.recent_packets != nullptr ? hooks.recent_packets(packets, kMaxRecentPackets) : 0;
  emit(hooks, "RTL_LORA_RECENT count=%u", static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    const RecentPacket& p = packets[i];
    char clean[sizeof(p.text)];
    size_t length = 0;
    for (; length + 1 < sizeof(clean) && p.text[length] != '\0'; ++length) {
      const char c = p.text[length];
      clean[length] = (c >= 32 && c <= 126 && c != '"') ? c : '.';
    }
    clean[length] = '\0';
    char snr[12] = "na";
    if (p.snr_tenths != INT16_MAX)
      snprintf(snr, sizeof(snr), "%.1f", static_cast<double>(p.snr_tenths) / 10.0);
    emit(hooks, "RTL_LORA_PKT age_ms=%lu sender=%08lx id=%lu port=%u encrypted=%d snr_db=%s text=\"%s\"",
         static_cast<unsigned long>(p.age_ms), static_cast<unsigned long>(p.sender),
         static_cast<unsigned long>(p.packet_id), static_cast<unsigned>(p.port),
         p.encrypted ? 1 : 0, snr, clean);
  }
  return true;
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (command == nullptr || strncmp(command, "RTL_LORA_", 9) != 0) return false;
  return handle_detector(command, authenticated, hooks) ||
         handle_decoder(command, authenticated, hooks) ||
         handle_stats(command, authenticated, hooks) ||
         handle_plan(command, authenticated, hooks) || handle_recent(command, hooks);
}

}  // namespace orcsdr::lora_diag
