#include "js8_frame.hpp"

#include <cassert>
#include <cstdio>

int main() {
  assert(orcsdr::js8::self_check_frame());
  std::puts("JS8 raw frame tests: PASS");
  return 0;
}
