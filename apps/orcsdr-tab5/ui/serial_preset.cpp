#include "serial_preset.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_preset {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[192];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_PRESET_LIST") == 0) {
    say(hooks, "RTL_PRESET_LIST_BEGIN count=%d\n", hooks.preset_count());
    for (int i = 0; i < hooks.preset_count(); ++i) {
      const Preset preset = hooks.preset(i);
      say(hooks, "RTL_PRESET %d frequency_hz=%u level=%.1f\n", i + 1, static_cast<unsigned>(preset.freq_hz),
          static_cast<double>(preset.level_dbfs));
    }
    hooks.write_line("RTL_PRESET_LIST_END");
    return true;
  }
  // Kept exactly as the dispatcher had it: the compare length is 17 although the literal is 16 characters long.
  if (strncmp(command, "RTL_PRESET_TUNE ", 17) == 0 && authenticated) {
    const int index = atoi(command + 17) - 1;
    if (index < 0 || index >= hooks.preset_count()) {
      hooks.write_line("RTL_PRESET_TUNE_INVALID index out of range");
      return true;
    }
    hooks.tune_fm(hooks.preset(index).freq_hz);
    hooks.persist_fm(hooks.preset(index).freq_hz);
    say(hooks, "RTL_PRESET_TUNE_OK index=%d frequency_hz=%u\n", index + 1, static_cast<unsigned>(hooks.preset(index).freq_hz));
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_preset
