#include "js8_mode.hpp"

#include <cassert>
#include <cstdio>

int main() {
  assert(orcsdr::js8::self_check());
  assert(orcsdr::js8::valid_submode(0));
  assert(!orcsdr::js8::valid_submode(5));
  std::puts("JS8 mode profile tests: PASS");
  return 0;
}
