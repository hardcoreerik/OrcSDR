// SPDX-License-Identifier: AGPL-3.0-only
// Host test of the self-test generator and runner: every case decodes exactly one block, the generator is deterministic,
// and a corrupted signal is detected as a failure (the runner is not a rubber stamp).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "acars_core.hpp"
#include "acars_selftest.hpp"

using namespace orcsdr;

namespace {
[[noreturn]] void fail(const char* e, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, e);
  std::exit(1);
}
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (false)

std::vector<uint8_t> all(acars_selftest::Signal& s) {
  std::vector<uint8_t> out;
  uint8_t buf[4096];
  for (size_t n; (n = s.next(buf, sizeof(buf))) > 0;) out.insert(out.end(), buf, buf + n);
  return out;
}
}  // namespace

int main() {
  const acars_selftest::Result r = acars_selftest::run(nullptr);
  CHECK(r.allocated);
  CHECK(r.cases == 10);
  if (r.passed != r.cases) std::printf("passed=%u missed=%u duplicates=%u\n", r.passed, r.missed, r.duplicates);
  CHECK(r.passed == r.cases && r.missed == 0 && r.duplicates == 0);
  CHECK(r.samples > 10 * 40000);

  acars_selftest::Signal a(3000, 0.05, 7), b(3000, 0.05, 7), c(3000, 0.05, 8);
  const auto va = all(a), vb = all(b), vc = all(c);
  CHECK(va == vb);                 // same seed, same signal
  CHECK(va != vc);                 // different noise
  CHECK(va.size() % 2 == 0);

  // A signal with a damaged middle must not decode: the runner's pass criterion is real.
  acars_selftest::Signal d(3000, 0.0, 1);
  auto vd = all(d);
  for (size_t i = vd.size() / 2; i < vd.size() / 2 + 6000; ++i) vd[i] = 127;
  acars::Frontend240k front;
  acars::Decoder dec;
  unsigned blocks = 0;
  dec.set_callback([](void* p, const acars::Message&) { ++*static_cast<unsigned*>(p); }, &blocks);
  front.reset();
  for (size_t pos = 0; pos < vd.size(); pos += 4096)
    front.process_cu8(vd.data() + pos, std::min<size_t>(4096, vd.size() - pos) / 2,
                      [](void* ctx, float i, float q) { static_cast<acars::Decoder*>(ctx)->process_channel_iq(i, q); }, &dec);
  CHECK(blocks == 0);
  std::puts("acars_selftest_tests: all passed");
  return 0;
}
