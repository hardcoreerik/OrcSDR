#include "source_capabilities.hpp"

#include <cstdio>

using namespace orcsdr::source_caps;

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

int main() {
  // No greeting yet: nothing is offered, so no control can claim to work.
  Caps none = remote(0, 0);
  CHECK(!none.stream && !none.retune && !none.gain && !none.gain_auto && !none.rtl_agc);

  // A greeted R820T-class server (tuner 5, 29 gain steps): streaming, retune, gain and both AGCs.
  Caps r820 = remote(5, 29);
  CHECK(r820.stream && r820.retune && r820.gain && r820.gain_auto && r820.rtl_agc);

  // A Blog V4 reports its own tuner type: same offer.
  Caps v4 = remote(6, 29);
  CHECK(v4.stream && v4.gain && v4.gain_auto);

  // A server that reports a tuner but no gain steps has no step table: auto gain only, no manual ladder.
  Caps nogains = remote(5, 0);
  CHECK(nogains.stream && !nogains.gain && nogains.gain_auto);

  // A greeting that gives gains but an unknown tuner type still counts as a greeting.
  Caps odd = remote(0, 14);
  CHECK(odd.stream && odd.gain);

  if (g_failures == 0) std::printf("SOURCE_CAPABILITIES_HOST_TEST failures=0 pass=1\n");
  else std::printf("SOURCE_CAPABILITIES_HOST_TEST failures=%d\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
