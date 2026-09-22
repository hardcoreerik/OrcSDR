#include "sd_benchmark_plan.hpp"

#include <cstdio>

#define CHECK(value) do { if (!(value)) { \
  std::fprintf(stderr, "FAIL line=%d check=%s\n", __LINE__, #value); return 1; \
} } while (false)

int main() {
  using namespace orcsdr::storage::benchmark;
  CHECK(kChunkBytes.size() == 5);
  CHECK(kChunkBytes[0] == 4 * 1024);
  CHECK(kChunkBytes[1] == 16 * 1024);
  CHECK(kChunkBytes[2] == 32 * 1024);
  CHECK(kChunkBytes[3] == 64 * 1024);
  CHECK(kChunkBytes[4] == 128 * 1024);
  CHECK(repetitions(16 * 1024) == 1);
  CHECK(repetitions(32 * 1024) == 3);
  CHECK(repetitions(64 * 1024) == 3);
  CHECK(mib_per_second(32 * 1024 * 1024, 4000000) == 8.0);
  CHECK(mib_per_second(1, 0) == 0.0);
  std::puts("sd_benchmark_plan_tests: PASS");
  return 0;
}
