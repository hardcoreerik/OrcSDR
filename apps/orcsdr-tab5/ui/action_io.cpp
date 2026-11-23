#include "action_io.hpp"

#include <cstdarg>
#include <cstdio>

namespace orcsdr::action_io {

void say(const Io& io, const char* format, ...) {
  char line[kSayMax];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length > 0) io.write(line);
}

}  // namespace orcsdr::action_io
