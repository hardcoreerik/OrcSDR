#pragma once
// The RTL_HELP serial command: the command listing, one line per entry.
//
// First of the serial command families moved out of process_command() in main.cpp. A family owns its routes: it is
// called from the dispatcher at the position its routes used to occupy, returns true when the line was one of its
// commands (handled or refused) and false to let the dispatcher carry on. Output goes through a hook so the family
// builds and is tested on the host. tools/serial_command_routes.py follows the call and checks that the order, the
// matched literals and the authentication checks are the same as before the move.
namespace orcsdr::serial_help {

struct Hooks {
  void (*write_line)(const char* text) = nullptr;   // prints one line and its line ending (Serial.println)
  bool orcdial = false;                             // this build includes the OrcDial bridge commands
};

// `command` must not be null. `authenticated` is unused: the listing is public.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_help
