#include "serial_web.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_web {

namespace {

// One formatted line, one write: what Serial.printf did for these replies (its buffer is 2048 bytes).
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
  if (strcmp(command, "RTL_WEB") == 0 || strcmp(command, "RTL_WEB_STATUS") == 0) {
    const Status status = hooks.status();
    say(hooks, "RTL_WEB_STATUS enabled=%d listening=%d url=%s\n",
        status.enabled ? 1 : 0,
        status.listening ? 1 : 0,
        status.url[0] ? status.url : "offline");
    return true;
  }
  if (strcmp(command, "RTL_WEB_AUDIO_STATUS") == 0) {
    hooks.print_audio_status();
    return true;
  }
  if ((strcmp(command, "RTL_WEB ON") == 0 || strcmp(command, "RTL_WEB OFF") == 0) &&
      authenticated) {
    const Status status = hooks.apply_enabled(command[8] == 'O' && command[9] == 'N');
    say(hooks, "RTL_WEB_OK enabled=%d listening=%d url=%s\n",
        status.enabled ? 1 : 0,
        status.listening ? 1 : 0,
        status.url[0] ? status.url : "offline");
    hooks.update_global_settings();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_web
