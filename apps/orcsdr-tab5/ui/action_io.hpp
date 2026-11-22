#pragma once
// Console and string helpers for the touch/action handler modules (shortwave_actions*, and the ones that follow).
// No device headers, no state. The handlers print through these two pointers so a host test can see every write.
#include <cstddef>
#include <cstring>

namespace orcsdr::action_io {

// Both pointers must be set.
struct Io {
  void (*write)(const char* text) = nullptr;        // as given, one console write (Serial.print)
  void (*write_line)(const char* text) = nullptr;   // the text, then the line ending (Serial.println)
};

constexpr size_t kSayMax = 256;   // every action line is shorter (the longest carries a 95-character error text)

// Format, then one io.write, as Serial.printf was (its buffer was 2,048 bytes).
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3), noinline))
#endif
void say(const Io& io, const char* format, ...);

// strlcpy on every platform: copies at most size - 1 characters, always terminates when size > 0.
inline size_t copy_text(char* destination, const char* source, size_t size) {
  const size_t length = strlen(source);
  if (size > 0) {
    const size_t n = length >= size ? size - 1 : length;
    memcpy(destination, source, n);
    destination[n] = '\0';
  }
  return length;
}

}  // namespace orcsdr::action_io
