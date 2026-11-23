#include "serial_ft8_io.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdio>

namespace orcsdr::serial_ft8 {

void say(const Io& io, const char* format, ...) {
  char line[kSayMax];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) io.write(line);
}

void say_wide(const Io& io, const char* format, ...) {
  char line[kSayWideMax];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) io.write(line);
}

bool refuse_signed_out(const Io& io, const char* verb, bool authenticated) {
  if (authenticated) return false;
  say(io, "ORC_FT8_ERROR %s auth_required\n", verb);
  return true;
}

size_t file_slug(const char* text, char* out, size_t size) {
  size_t n = 0;
  for (const char* p = text; *p != '\0' && n + 1 < size; ++p)
    if (isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '-') out[n++] = *p;
  if (size > 0) out[n] = '\0';
  return n;
}

}  // namespace orcsdr::serial_ft8
