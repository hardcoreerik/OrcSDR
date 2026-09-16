#include "shortwave_model.hpp"

#include <cstdio>

int main() {
  const bool passed = orcsdr::shortwave::model_self_check();
  std::printf("SHORTWAVE_MODEL_HOST_TEST pass=%d\n", passed ? 1 : 0);
  return passed ? 0 : 1;
}
