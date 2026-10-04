#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "keyboard_input.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::keyboard_input;

// Builds a character event: modifier byte then the name bytes.
size_t event(uint8_t* out, uint8_t modifier, const char* name) {
  out[0] = modifier;
  const size_t length = std::strlen(name);
  std::memcpy(out + 1, name, length);
  return length + 1;
}

bool decode(uint8_t modifier, const char* name, Key* key) {
  uint8_t data[20];
  const size_t length = event(data, modifier, name);
  return decode_char_event(data, length, key);
}

void test_printable_characters() {
  Key key;
  CHECK(decode(0, "a", &key));
  CHECK(key.special == Special::none && key.ch == 'a' && !key.ctrl && !key.alt);
  CHECK(decode(0, "A", &key) && key.ch == 'A');
  CHECK(decode(0, "7", &key) && key.ch == '7');
  CHECK(decode(0, " ", &key) && key.ch == ' ');          // space is a one-character name
  CHECK(decode(0, "\\", &key) && key.ch == '\\');
  CHECK(decode(0, "\"", &key) && key.ch == '"');
  CHECK(decode(0, "~", &key) && key.ch == '~');
}

void test_special_keys_case_insensitive() {
  Key key;
  const struct { const char* name; Special special; } cases[] = {
      {"esc", Special::escape},   {"ESC", Special::escape},
      {"del", Special::delete_key}, {"DEL", Special::delete_key},
      {"tab", Special::tab},
      {"backspace", Special::backspace}, {"BACKSPACE", Special::backspace},
      {"enter", Special::enter},  {"ENTER", Special::enter},
      {"up", Special::up}, {"UP", Special::up},
      {"down", Special::down}, {"left", Special::left}, {"RIGHT", Special::right},
  };
  for (const auto& c : cases) {
    CHECK(decode(0, c.name, &key));
    CHECK(key.special == c.special);
  }
}

void test_modifiers() {
  Key key;
  CHECK(decode(kModCtrl, "u", &key) && key.ctrl && !key.alt && key.ch == 'u');
  CHECK(decode(kModAlt, "x", &key) && key.alt && !key.ctrl);
  CHECK(decode(kModCtrl | kModAlt, "c", &key) && key.ctrl && key.alt);
  CHECK(decode(0x02, "a", &key) && !key.ctrl && !key.alt);   // shift bit alone is not a chord
}

void test_malformed_events_rejected() {
  Key key;
  uint8_t data[20] = {0, 'a'};
  CHECK(!decode_char_event(nullptr, 2, &key));
  CHECK(!decode_char_event(data, 2, nullptr));
  CHECK(!decode_char_event(data, 0, &key));
  CHECK(!decode_char_event(data, 1, &key));                    // modifier only, no name
  CHECK(!decode_char_event(data, 18, &key));                   // longer than the device can send
  CHECK(!decode(0, "nonsense", &key));
  CHECK(!decode(0, "ente", &key));                             // prefix of a name
  CHECK(!decode(0, "enterx", &key));
  const uint8_t control[] = {0, 0x07};                         // BEL
  CHECK(!decode_char_event(control, sizeof(control), &key));
  const uint8_t high[] = {0, 0xC3};                            // non-ASCII byte
  CHECK(!decode_char_event(high, sizeof(high), &key));
}

Key ch(char c) { Key k; k.ch = c; return k; }
Key sp(Special s) { Key k; k.special = s; return k; }

void test_typing_and_backspace() {
  char buffer[16] = "";
  CHECK(apply_edit(buffer, sizeof(buffer), 8, ch('h')) == EditAction::edited);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, ch('i')) == EditAction::edited);
  CHECK(std::strcmp(buffer, "hi") == 0);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::backspace)) == EditAction::edited);
  CHECK(std::strcmp(buffer, "h") == 0);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::delete_key)) == EditAction::edited);
  CHECK(std::strcmp(buffer, "") == 0);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::backspace)) == EditAction::none);
}

void test_length_limit() {
  char buffer[8] = "";
  for (char c : {'1', '2', '3', '4'})
    CHECK(apply_edit(buffer, sizeof(buffer), 4, ch(c)) == EditAction::edited);
  CHECK(apply_edit(buffer, sizeof(buffer), 4, ch('5')) == EditAction::none);   // at the maximum
  CHECK(std::strcmp(buffer, "1234") == 0);
  // A maximum larger than the buffer is clamped so the terminator always fits.
  char tiny[3] = "";
  CHECK(apply_edit(tiny, sizeof(tiny), 99, ch('a')) == EditAction::edited);
  CHECK(apply_edit(tiny, sizeof(tiny), 99, ch('b')) == EditAction::edited);
  CHECK(apply_edit(tiny, sizeof(tiny), 99, ch('c')) == EditAction::none);
  CHECK(std::strcmp(tiny, "ab") == 0);
}

void test_actions() {
  char buffer[16] = "wifi";
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::enter)) == EditAction::accept);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::escape)) == EditAction::cancel);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::tab)) == EditAction::toggle_reveal);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, sp(Special::left)) == EditAction::none);
  CHECK(std::strcmp(buffer, "wifi") == 0);                     // none of those touch the text
}

void test_chords() {
  char buffer[16] = "secret";
  Key ctrl_u; ctrl_u.ch = 'u'; ctrl_u.ctrl = true;
  CHECK(apply_edit(buffer, sizeof(buffer), 8, ctrl_u) == EditAction::edited);
  CHECK(std::strcmp(buffer, "") == 0);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, ctrl_u) == EditAction::none);
  // Other Ctrl / Alt chords are never inserted as text.
  Key ctrl_a; ctrl_a.ch = 'a'; ctrl_a.ctrl = true;
  Key alt_a; alt_a.ch = 'a'; alt_a.alt = true;
  CHECK(apply_edit(buffer, sizeof(buffer), 8, ctrl_a) == EditAction::none);
  CHECK(apply_edit(buffer, sizeof(buffer), 8, alt_a) == EditAction::none);
  CHECK(std::strcmp(buffer, "") == 0);
}

void test_invalid_arguments() {
  Key k = ch('a');
  CHECK(apply_edit(nullptr, 8, 4, k) == EditAction::none);
  char buffer[4] = "";
  CHECK(apply_edit(buffer, 0, 4, k) == EditAction::none);
  Key bad; bad.ch = '\x01';
  CHECK(apply_edit(buffer, sizeof(buffer), 3, bad) == EditAction::none);
}

}  // namespace

int main() {
  test_printable_characters();
  test_special_keys_case_insensitive();
  test_modifiers();
  test_malformed_events_rejected();
  test_typing_and_backspace();
  test_length_limit();
  test_actions();
  test_chords();
  test_invalid_arguments();
  CHECK(orcsdr::keyboard_input::self_check());
  std::puts("keyboard_input_tests: PASS");
  return 0;
}
