#include "keyboard_input.hpp"

#include <cstring>

namespace orcsdr::keyboard_input {
namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Case-insensitive: the keyboard reports special keys in upper case while Aa is active.
bool name_is(const char* name, size_t length, const char* expected) {
  if (std::strlen(expected) != length) return false;
  for (size_t i = 0; i < length; ++i)
    if (lower(name[i]) != expected[i]) return false;
  return true;
}

Special special_for(const char* name, size_t length) {
  struct Entry { const char* name; Special special; };
  static constexpr Entry kNames[] = {
      {"esc", Special::escape}, {"del", Special::delete_key}, {"tab", Special::tab},
      {"backspace", Special::backspace}, {"enter", Special::enter}, {"up", Special::up},
      {"down", Special::down}, {"left", Special::left}, {"right", Special::right},
  };
  for (const Entry& entry : kNames)
    if (name_is(name, length, entry.name)) return entry.special;
  return Special::none;
}

}  // namespace

bool decode_char_event(const uint8_t* data, size_t length, Key* out) {
  if (data == nullptr || out == nullptr || length < 2 || length > 17) return false;
  Key key;
  key.ctrl = (data[0] & kModCtrl) != 0;
  key.alt = (data[0] & kModAlt) != 0;
  const char* name = reinterpret_cast<const char*>(data + 1);
  const size_t name_length = length - 1;
  if (name_length == 1) {
    const uint8_t c = static_cast<uint8_t>(name[0]);
    if (c < 0x20 || c > 0x7E) return false;
    key.ch = static_cast<char>(c);
  } else {
    key.special = special_for(name, name_length);
    if (key.special == Special::none) return false;
  }
  *out = key;
  return true;
}

EditAction apply_edit(char* buffer, size_t capacity, size_t maximum, const Key& key) {
  if (buffer == nullptr || capacity == 0) return EditAction::none;
  if (maximum >= capacity) maximum = capacity - 1;
  size_t length = std::strlen(buffer);
  switch (key.special) {
    case Special::enter: return EditAction::accept;
    case Special::escape: return EditAction::cancel;
    case Special::tab: return EditAction::toggle_reveal;
    case Special::backspace:
    case Special::delete_key:
      if (length == 0) return EditAction::none;
      buffer[length - 1] = '\0';
      return EditAction::edited;
    case Special::up:
    case Special::down:
    case Special::left:
    case Special::right:
      return EditAction::none;
    case Special::none:
      break;
  }
  if (key.ctrl && lower(key.ch) == 'u') {
    if (length == 0) return EditAction::none;
    buffer[0] = '\0';
    return EditAction::edited;
  }
  // Ctrl/Alt chords are shortcuts, never text.
  if (key.ctrl || key.alt) return EditAction::none;
  if (key.ch < 0x20 || key.ch > 0x7E) return EditAction::none;
  if (length >= maximum) return EditAction::none;
  buffer[length] = key.ch;
  buffer[length + 1] = '\0';
  return EditAction::edited;
}

bool self_check() {
  Key key;
  const uint8_t letter[] = {0, 'a'};
  const uint8_t upper_enter[] = {0, 'E', 'N', 'T', 'E', 'R'};
  char buffer[8] = "ab";
  return decode_char_event(letter, sizeof(letter), &key) && key.ch == 'a' &&
         decode_char_event(upper_enter, sizeof(upper_enter), &key) &&
         key.special == Special::enter &&
         apply_edit(buffer, sizeof(buffer), 7, Key{Special::none, 'c', false, false}) ==
             EditAction::edited &&
         std::strcmp(buffer, "abc") == 0;
}

}  // namespace orcsdr::keyboard_input
