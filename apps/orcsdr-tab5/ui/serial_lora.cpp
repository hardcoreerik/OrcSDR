#include "serial_lora.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_lora {

namespace {

// One formatted line, one write: what Serial.printf did for these replies (same 2048-byte buffer).
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[2048];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

struct FileListing {
  const Hooks* hooks;
  unsigned count;
};

void emit_history_file(void* context, unsigned long long bytes, const char* name) {
  FileListing* listing = static_cast<FileListing*>(context);
  say(*listing->hooks, "LORA_HISTORY_FILE bytes=%llu path=/orcsdr/lora/%s\n", bytes, name);
  ++listing->count;
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_LORA_REGION_LIST") == 0) {
    for (size_t i = 0; i < hooks.region_count(); ++i) {
      const Region region = hooks.region_at(i);
      say(hooks, "RTL_LORA_REGION index=%u code=%s slots=%u default_slot=%u\n", static_cast<unsigned>(i + 1),
          region.code, region.slots, region.default_slot);
    }
    return true;
  }
  if ((strcmp(command, "RTL_LORA_AUTO ON") == 0 ||
       strcmp(command, "RTL_LORA_AUTO OFF") == 0) &&
      authenticated) {
    const bool enabled = command[14] == 'O' && command[15] == 'N';
    hooks.set_detector(enabled);
    say(hooks, "RTL_LORA_AUTO %s\n", enabled ? "ON" : "OFF");
    return true;
  }
  if (strncmp(command, "RTL_LORA_TUNE ", 14) == 0 && authenticated) {
    char* end = nullptr;
    const unsigned long requested = strtoul(command + 14, &end, 10);
    if (end == command + 14 || *end != '\0' || requested < hooks.min_hz ||
        requested > hooks.max_hz) {
      say(hooks, "RTL_LORA_TUNE_ERROR range=%u-%u\n", static_cast<unsigned>(hooks.min_hz),
          static_cast<unsigned>(hooks.max_hz));
      return true;
    }
    const uint32_t frequency_hz = static_cast<uint32_t>(requested);
    hooks.tune(frequency_hz);
    say(hooks, "RTL_LORA_TUNE_OK frequency_hz=%u\n", static_cast<unsigned>(frequency_hz));
    return true;
  }
  if (strcmp(command, "LORA_SD_LOG ON") == 0) {
    hooks.set_sd_logging(true);
    return true;
  }
  if (strcmp(command, "LORA_SD_LOG OFF") == 0) {
    hooks.set_sd_logging(false);
    return true;
  }
  if (strcmp(command, "LORA_HISTORY FILES") == 0) {
    FileListing listing{&hooks, 0};
    switch (hooks.history_files(&listing, emit_history_file)) {
      case HistoryFiles::radio_busy: hooks.write_line("LORA_HISTORY_ERROR radio_busy"); return true;
      case HistoryFiles::sd_unavailable: hooks.write_line("LORA_HISTORY_ERROR sd_unavailable"); return true;
      case HistoryFiles::open_failed: hooks.write_line("LORA_HISTORY_ERROR open_failed"); return true;
      case HistoryFiles::ok: break;
    }
    say(hooks, "LORA_HISTORY_FILES_DONE count=%u\n", listing.count);
    return true;
  }
  if (strcmp(command, "LORA_HISTORY STATUS") == 0) {
    const HistoryStatus status = hooks.history_status();
    say(hooks, "LORA_HISTORY_STATUS loaded=%u nodes=%u replayed=%lu rejected=%lu written=%lu error=%u directory=/orcsdr/lora\n",
        status.loaded, status.nodes, status.replayed, status.rejected, status.written, status.error);
    return true;
  }
  if (strncmp(command, "LORA_HISTORY NODE ", 18) == 0) {
    uint32_t id = 0;
    if (!hooks.parse_hex_u32(command + 18, &id)) { hooks.write_line("LORA_HISTORY_ERROR invalid_id"); return true; }
    HistoryNode node{};
    if (!hooks.history_node(id, &node)) { hooks.write_line("LORA_HISTORY_ERROR not_found"); return true; }
    say(hooks, "LORA_HISTORY_NODE first_utc=%lu packets=%lu name_utc=%lu position_utc=%lu data=%s",
        node.first_utc, node.packets, node.name_utc, node.position_utc, node.data);
    return true;
  }
  if (strcmp(command, "LORA_SD_LOG STATUS") == 0) {
    const SdLogStatus status = hooks.sd_log_status();
    say(hooks,
        "LORA_SD_LOG_STATUS requested=%s ready=%s error=%s queued=%u "
        "dropped=%lu path=\"%s\" pending=%u retry_attempts=%lu last_error_stage=%s last_errno=%d\n",
        status.requested ? "true" : "false", status.ready ? "true" : "false", status.error ? "true" : "false",
        status.queued, status.dropped, status.path, status.pending, status.retry_attempts,
        status.last_error_stage, status.last_errno);
    return true;
  }
  if (strncmp(command, "LORA_MESSAGE ", 13) == 0) {
    if (!hooks.present_message(command + 13)) {
      hooks.write_line("LORA_MESSAGE_ERROR invalid_fields");
      return true;
    }
    hooks.write_line("LORA_MESSAGE_OK");
    return true;
  }
  if (strncmp(command, "LORA_PACKET ", 12) == 0) {
    if (!hooks.present_packet(command + 12)) {
      hooks.write_line("LORA_PACKET_ERROR invalid_fields");
      return true;
    }
    hooks.write_line("LORA_PACKET_OK");
    return true;
  }
  if (strcmp(command, "LORA_MESSAGE_CLEAR") == 0) {
    hooks.clear_messages();
    hooks.write_line("LORA_MESSAGE_CLEARED");
    return true;
  }
  if (strcmp(command, "RTL_LORA_MEMORY") == 0) {
    hooks.log_memory();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_lora
