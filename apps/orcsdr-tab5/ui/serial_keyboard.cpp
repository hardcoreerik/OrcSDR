#include "serial_keyboard.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orcsdr::serial_keyboard {

namespace {

// One formatted line, one write: what Serial.printf did for these replies.
void say(const Hooks& hooks, const char* format, ...) __attribute__((format(printf, 2, 3)));
void say(const Hooks& hooks, const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) hooks.write(line);
}

}  // namespace

bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {
  if (strcmp(command, "RTL_KEYBOARD STATUS") == 0) {
    const auto keyboard = hooks.status();
    say(hooks,
        "RTL_KEYBOARD_STATUS present=%d firmware=%u mode=%u int_low=%d keys=%lu "
        "dropped=%lu undecoded=%lu bus_errors=%lu attaches=%lu echo=%d editor=%d\n",
        keyboard.present ? 1 : 0, static_cast<unsigned>(keyboard.firmware), static_cast<unsigned>(keyboard.mode),
        keyboard.int_low ? 1 : 0, static_cast<unsigned long>(keyboard.keys),
        static_cast<unsigned long>(keyboard.dropped), static_cast<unsigned long>(keyboard.undecoded),
        static_cast<unsigned long>(keyboard.bus_errors), static_cast<unsigned long>(keyboard.attach_count),
        hooks.echo() ? 1 : 0, hooks.editor_active() ? 1 : 0);
    const Focus focus = hooks.focus();
    if (focus.focused)
      say(hooks, "RTL_KEYBOARD_FOCUS controls=%u x=%d y=%d w=%d h=%d ring=%d\n",
          static_cast<unsigned>(focus.controls), focus.x, focus.y, focus.w, focus.h, focus.ring ? 1 : 0);
    else
      say(hooks, "RTL_KEYBOARD_FOCUS controls=%u none\n", static_cast<unsigned>(focus.controls));
    return true;
  }
  if (strncmp(command, "RTL_KEYBOARD KEY ", 17) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_KEYBOARD_ERROR auth_required");
      return true;
    }
    using orcsdr::keyboard_input::Special;
    orcsdr::keyboard_input::Key key;
    const char* name = command + 17;
    if (strcmp(name, "UP") == 0) key.special = Special::up;
    else if (strcmp(name, "DOWN") == 0) key.special = Special::down;
    else if (strcmp(name, "LEFT") == 0) key.special = Special::left;
    else if (strcmp(name, "RIGHT") == 0) key.special = Special::right;
    else if (strcmp(name, "ENTER") == 0) key.special = Special::enter;
    else if (strcmp(name, "ESC") == 0) key.special = Special::escape;
    else if (strcmp(name, "BACKSPACE") == 0) key.special = Special::backspace;
    else if (strcmp(name, "TAB") == 0) key.special = Special::tab;
    else if (strcmp(name, "SPACE") == 0) key.ch = ' ';
    else if (name[0] != '\0' && name[1] == '\0') key.ch = name[0];
    else {
      hooks.write_line("RTL_KEYBOARD_INVALID use UP|DOWN|LEFT|RIGHT|ENTER|ESC|TAB|SPACE|BACKSPACE|<char>");
      return true;
    }
    hooks.route_key(key);
    hooks.write_line("RTL_KEYBOARD_KEY_OK");
    return true;
  }
  if (strncmp(command, "RTL_KEYBOARD ECHO ", 18) == 0) {
    if (!authenticated) {
      hooks.write_line("RTL_KEYBOARD_ERROR auth_required");
      return true;
    }
    const bool echo = hooks.set_echo(command[18] == '1');
    say(hooks, "RTL_KEYBOARD_ECHO %d\n", echo ? 1 : 0);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_keyboard
