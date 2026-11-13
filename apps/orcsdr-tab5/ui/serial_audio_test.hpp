#pragma once
// The RTL_AUDIO_TEST serial commands: STATUS, TONE, FM and STOP.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order and the speaker calls are the ones the dispatcher had. Every action comes through Hooks, so the
// family builds and is tested on the host; it keeps no state of its own. Each reply is printed by the hook itself.
namespace orcsdr::serial_audio_test {

// Every pointer must be set.
struct Hooks {
  void (*test_status)() = nullptr;                  // RTL_AUDIO_TEST STATUS: prints the whole reply itself
  void (*test_tone)() = nullptr;                    // RTL_AUDIO_TEST TONE
  void (*test_fm)() = nullptr;                      // RTL_AUDIO_TEST FM
  void (*test_stop)() = nullptr;                    // RTL_AUDIO_TEST STOP
};

// Returns true when `command` (not null) was one of these audio test commands.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_audio_test
