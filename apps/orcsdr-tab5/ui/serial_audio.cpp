#include "serial_audio.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_audio {

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
  (void)authenticated;
  if (strcmp(command, "RTL_VOLUME") == 0) {
    say(hooks, "RTL_VOLUME_STATUS volume=%u\n", static_cast<unsigned>(hooks.volume()));
    return true;
  }
  if (strcmp(command, "RTL_SOUND") == 0) {
    say(hooks, "RTL_SOUND_STATUS enabled=%d\n", hooks.sound_enabled() ? 1 : 0);
    return true;
  }
  if (strcmp(command, "RTL_SOUND ON") == 0 || strcmp(command, "RTL_SOUND OFF") == 0) {
    const bool enable = command[10] == 'O' && command[11] == 'N';
    hooks.set_sound_enabled(enable);
    say(hooks, "RTL_SOUND_OK enabled=%d\n", enable ? 1 : 0);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_audio
