#include "serial_audio_test.hpp"

#include <cstring>

namespace orcsdr::serial_audio_test {

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  (void)authenticated;
  if (strcmp(command, "RTL_AUDIO_TEST STATUS") == 0) {
    hooks.test_status();
    return true;
  }
  if (strcmp(command, "RTL_AUDIO_TEST TONE") == 0) {
    hooks.test_tone();
    return true;
  }
  if (strcmp(command, "RTL_AUDIO_TEST FM") == 0) {
    hooks.test_fm();
    return true;
  }
  if (strcmp(command, "RTL_AUDIO_TEST STOP") == 0) {
    hooks.test_stop();
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_audio_test
