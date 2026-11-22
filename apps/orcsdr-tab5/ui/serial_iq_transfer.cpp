#include "serial_iq_transfer.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_iq_transfer {
namespace {

void say(const Hooks& hooks, const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "RTL_LORA_REPLAY_BEGIN ", 22) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_LORA_REPLAY_ERROR auth_required");
    } else {
      hooks.replay_begin(command + 22);
    }
    return true;
  }
  if (strncmp(command, "RTL_LORA_REPLAY_CHUNK ", 22) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_LORA_REPLAY_ERROR auth_required");
    } else {
      hooks.replay_chunk(command + 22);
    }
    return true;
  }
  if (strcmp(command, "RTL_LORA_REPLAY_ABORT") == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_LORA_REPLAY_ERROR auth_required");
    } else {
      hooks.replay_abort();
    }
    return true;
  }
  if (strcmp(command, "RTL_IQ_RETRIEVE_BEGIN") == 0) {
    RetrieveInfo info;
    if (!hooks.retrieve_begin(&info)) {
      hooks.write_line("RTL_IQ_RETRIEVE_ERROR capture_not_ready");
      return true;
    }
    hooks.write("RTL_IQ_RETRIEVE_READY storage=psram");
    say(hooks, " bytes=%u rate=%u frequency_hz=%u sf=%u bw=%u\n", static_cast<unsigned>(info.bytes),
        static_cast<unsigned>(info.rate), static_cast<unsigned>(info.frequency_hz), static_cast<unsigned>(info.sf),
        static_cast<unsigned>(info.bandwidth_hz));
    return true;
  }
  if (strcmp(command, "RTL_IQ_GET_BEGIN") == 0) {
    hooks.get_begin();
    return true;
  }
  if (strcmp(command, "RTL_IQ_GET_CHUNK") == 0) {
    hooks.get_chunk();
    return true;
  }
  if (strcmp(command, "RTL_IQ_GET_ABORT") == 0) {
    const AbortInfo info = hooks.get_abort();
    say(hooks, "RTL_IQ_GET_ABORTED ready=%s bytes=%u\n", info.ready ? "true" : "false",
        static_cast<unsigned>(info.bytes));
    return true;
  }
  if (strcmp(command, "RTL_IQ_RETRIEVE_END") == 0) {
    hooks.write_line(hooks.retrieve_end() ? "RTL_IQ_RETRIEVE_RESUMING" : "RTL_IQ_RETRIEVE_DONE");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_iq_transfer
