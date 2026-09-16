#include "receiver_tuning_controls.hpp"

#include <cstdio>

int main() {
  const bool passed = orcsdr::receiver_controls::self_check();
  std::printf("RECEIVER_TUNING_CONTROLS_HOST_TEST pass=%d\n", passed ? 1 : 0);
  return passed ? 0 : 1;
}
