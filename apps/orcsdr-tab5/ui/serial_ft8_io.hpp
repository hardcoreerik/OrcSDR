#pragma once
// Console helpers shared by the FT8 serial command modules (serial_ft8*.cpp). No device headers, no state.
#include <cstddef>

namespace orcsdr::serial_ft8 {

// Both pointers must be set.
struct Io {
  void (*write)(const char* text) = nullptr;        // as given, one console write (Serial.print)
  void (*write_line)(const char* text) = nullptr;   // the text, then the line ending (Serial.println)
};

constexpr size_t kSayMax = 256;       // every FT8 reply line except STATUS fits (longest: about 200 bytes)
constexpr size_t kSayWideMax = 896;   // the STATUS line (longest possible: checked in the host test)

// Format, then one io.write, as Serial.printf was (its buffer was 2,048 bytes).
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3), noinline))
#endif
void say(const Io& io, const char* format, ...);
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3), noinline))
#endif
void say_wide(const Io& io, const char* format, ...);

// The sign-in rule of the state-changing verbs: prints "ORC_FT8_ERROR <verb> auth_required" and returns true when
// signed out.
bool refuse_signed_out(const Io& io, const char* verb, bool authenticated);

// Keeps letters, digits, '_' and '-' of `text` (a file name typed on the command line); at most size - 1 characters.
// Returns the length written. `out` is always terminated.
size_t file_slug(const char* text, char* out, size_t size);

}  // namespace orcsdr::serial_ft8
