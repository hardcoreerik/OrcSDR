#include "serial_lora_status.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_lora_status {

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

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "RTL_LORA_NATIVE_STATUS") == 0) {
    const NativeStatus status = hooks.native_status();
    say(hooks, "RTL_LORA_NATIVE_STATUS ready=%s busy=%s key_loaded=%s crc_ok=%lu encrypted=%lu failures=%lu last_decode_ms=%lu preambles=%lu header_failures=%lu crc_failures=%lu raw_cfo_hz=%.1f cfo_hz=%.1f\n",
        status.ready ? "true" : "false", status.busy ? "true" : "false", status.key_loaded ? "true" : "false",
        status.crc_ok, status.encrypted, status.failures, status.last_decode_ms, status.preambles,
        status.header_failures, status.crc_failures, status.raw_cfo_hz, status.cfo_hz);
    return true;
  }
  if (strcmp(command, "RTL_LORA_PLAN_STATUS") == 0) {
    const PlanStatus plan = hooks.plan_status();
    say(hooks, "RTL_LORA_PLAN_STATUS region_index=%u region=%s slot=%u slots=%u "
        "default_slot=%u frequency_hz=%u profile=LONGFAST persisted=%s\n",
        plan.region_index, plan.region_name, plan.slot, plan.slots, plan.default_slot,
        static_cast<unsigned>(plan.frequency_hz), plan.persisted ? "true" : "false");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_lora_status
