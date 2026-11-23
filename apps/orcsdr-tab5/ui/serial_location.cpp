#include "serial_location.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_location {

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
  if (strcmp(command, "RTL_LOCATION STATUS") == 0) {
    const auto location = hooks.state();
    say(hooks, "RTL_LOCATION_STATUS busy=%d ready=%d latitude_e7=%ld longitude_e7=%ld message=\"%s\"\n",
        location.busy ? 1 : 0, location.ready ? 1 : 0,
        static_cast<long>(location.latitude_e7),
        static_cast<long>(location.longitude_e7), location.message);
    return true;
  }
  if (strcmp(command, "RTL_LOCATION IP") == 0 ||
      strncmp(command, "RTL_LOCATION LOOKUP ", 20) == 0 ||
      strcmp(command, "RTL_LOCATION CONFIRM") == 0) {
    if (!authenticated) { hooks.write_line("RTL_LOCATION_ERROR auth_required"); return true; }
    if (strcmp(command, "RTL_LOCATION CONFIRM") == 0) {
      hooks.confirm();
      say(hooks, "RTL_LOCATION_CONFIRM_%s\n",
          hooks.state().ready ? "OK" : "REJECTED");
      return true;
    }
    const char* query = strncmp(command, "RTL_LOCATION LOOKUP ", 20) == 0
                            ? command + 20 : nullptr;
    if (query && (!query[0] || strlen(query) >= 64)) {
      hooks.write_line("RTL_LOCATION_ERROR query_length");
      return true;
    }
    const bool queued = query ? hooks.request_lookup(query)
                              : hooks.request_ip();
    say(hooks, "RTL_LOCATION_%s_%s\n", query ? "LOOKUP" : "IP",
        queued ? "QUEUED" : "REJECTED");
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_location
