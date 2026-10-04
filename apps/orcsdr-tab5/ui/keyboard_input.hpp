#pragma once

#include <cstddef>
#include <cstdint>

// Hardware-independent keyboard input model. The M5Stack Tab5 Keyboard (character mode)
// reports each key press as a modifier byte followed by the key's name string; this turns that
// into a key and applies it to a text buffer. No I2C, display or RTOS dependencies, so it is
// unit-tested on the host.
namespace orcsdr::keyboard_input {

enum class Special : uint8_t {
  none,
  escape,
  delete_key,
  tab,
  backspace,
  enter,
  up,
  down,
  left,
  right,
};

struct Key {
  Special special = Special::none;
  char ch = '\0';   // printable ASCII when special == none
  bool ctrl = false;
  bool alt = false;
};

// HID modifier bits carried in the first byte of a character event.
constexpr uint8_t kModCtrl = 0x01;
constexpr uint8_t kModAlt = 0x04;

// Decodes one character event: data[0] is the modifier mask and data[1..length-1] the key name
// or the typed character. Returns false for empty, malformed or unrecognised names.
bool decode_char_event(const uint8_t* data, size_t length, Key* out);

enum class EditAction : uint8_t {
  none,           // key had no effect on the text
  edited,         // buffer changed
  accept,         // Enter
  cancel,         // Esc
  toggle_reveal,  // Tab: show or hide a masked field
};

// Applies a key to a NUL-terminated buffer holding at most `maximum` characters
// (maximum < capacity). Printable ASCII appends, Backspace/Delete remove the last character,
// Ctrl+U clears the line, Enter accepts, Esc cancels, Tab toggles reveal.
EditAction apply_edit(char* buffer, size_t capacity, size_t maximum, const Key& key);

bool self_check();

}  // namespace orcsdr::keyboard_input
