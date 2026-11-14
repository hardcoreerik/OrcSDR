#include "serial_iq.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_iq {
namespace {

void say(const Hooks& hooks, const char* format, ...) {
  char line[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_CAPTURE_STATUS") == 0) {
    const CaptureStatus s = hooks.capture_status();
    if (!s.finished) {
      say(hooks, "RTL_CAPTURE_STATUS state=%s\n", s.state);
      return true;
    }
    say(hooks, "RTL_CAPTURE_STATUS state=%s bytes=%llu min=%u max=%u mean=%.3f sha256=%s error=\"%s\"\n", s.state,
        static_cast<unsigned long long>(s.bytes), s.min, s.max, s.mean, s.sha256[0] == '\0' ? "none" : s.sha256,
        s.error);
    return true;
  }
  if (strcmp(command, "RTL_IQ_START") == 0) {
    hooks.iq_start();
    return true;
  }
  if (strncmp(command, "RTL_IQ_DIAG_START ", 18) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_IQ_DIAG_ERROR auth_required");
    } else {
      hooks.iq_diag_start(command + 18);
    }
    return true;
  }
  if (strcmp(command, "RTL_IQ_DIAG_STATUS") == 0) {
    const DiagStatus s = hooks.diag_status();
    say(hooks,
        "RTL_IQ_DIAG_STATUS transition=\"%s\" sequence=%lu active=%d ready=%d "
        "bytes=%u max_bytes=%u rate=%u frequency_hz=%u started_ms=%lu\n",
        s.transition, static_cast<unsigned long>(s.sequence), s.active ? 1 : 0, s.ready ? 1 : 0,
        static_cast<unsigned>(s.bytes), static_cast<unsigned>(s.max_bytes), static_cast<unsigned>(s.rate),
        static_cast<unsigned>(s.frequency_hz), static_cast<unsigned long>(s.started_ms));
    return true;
  }
  if (strcmp(command, "RTL_IQ_STOP") == 0 || strcmp(command, "RTL_IQ_SAVE") == 0) {
    hooks.iq_stop();
    return true;
  }
  if (strcmp(command, "RTL_IQ_STATUS") == 0) {
    const IqStatus s = hooks.iq_status();
    say(hooks,
        "RTL_IQ_STATUS source=%s active=%s ready=%s storage=%s mode=%s bytes=%u max_bytes=%u frequency_hz=%u sf=%u "
        "bw=%u auto=%s events=%u messages=%u noise_dbfs=%.1f trigger_dbfs=%.1f last_path=\"%s\"\n",
        s.source, s.active ? "true" : "false", s.ready ? "true" : "false", "psram",
        s.energy_triggered ? "energy" : "manual", static_cast<unsigned>(s.bytes), static_cast<unsigned>(s.max_bytes),
        static_cast<unsigned>(s.frequency_hz), static_cast<unsigned>(s.sf), static_cast<unsigned>(s.bandwidth_hz),
        s.detector_on ? "on" : "off", static_cast<unsigned>(s.events), static_cast<unsigned>(s.messages),
        s.noise_dbfs, s.trigger_dbfs, s.last_path[0] ? s.last_path : "none");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_iq
