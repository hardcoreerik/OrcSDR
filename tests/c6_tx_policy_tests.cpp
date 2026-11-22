// Host test for c6_tx_policy: the adaptive C6 transmit-power cap (full -> 15 dBm -> 11 dBm on supply restarts, one step
// back up after 10 minutes of stable link).
#include "c6_tx_policy.hpp"

#include <cstdio>
#include <initializer_list>

namespace p = orcsdr::c6_tx_policy;
static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++g_failures; } } while (0)

int main() {
  {  // a fresh unit runs at full power and a stable link changes nothing
    p::State s;
    CHECK(p::cap_qdbm(s) == 0);
    CHECK(!p::service(s, true, 1000) && !p::service(s, true, 1000 + p::kStepUpMs * 3));
    CHECK(p::cap_qdbm(s) == 0);
  }
  {  // brown-out and power-on restarts step down one level each, to 11 dBm and no lower
    p::State s;
    CHECK(p::on_c6_restart(s, p::kResetBrownout, 5000) && p::cap_qdbm(s) == 60);
    CHECK(p::on_c6_restart(s, p::kResetPowerOn, 6000) && p::cap_qdbm(s) == 44);
    CHECK(!p::on_c6_restart(s, p::kResetPowerGlitch, 7000) && p::cap_qdbm(s) == 44);
    CHECK(s.supply_restarts == 3);
  }
  {  // other reasons (C6 crash, software reset, unknown) do not touch the cap
    p::State s;
    for (int32_t r : {0, 2, 3, 4, 5, 6, 7, 8, 10, 11, 15, -1}) CHECK(!p::on_c6_restart(s, r, 1000));
    CHECK(p::cap_qdbm(s) == 0 && s.supply_restarts == 0);
  }
  {  // a link failure steps down the same way (the trigger used with a C6 that reports reason 0)
    p::State s;
    CHECK(p::on_link_failure(s, 1000) && p::cap_qdbm(s) == 60);
    CHECK(p::on_link_failure(s, 1000) && p::cap_qdbm(s) == 44);
    CHECK(!p::on_link_failure(s, 1000) && p::cap_qdbm(s) == 44 && s.supply_restarts == 3);
  }
  {  // one step back up per 10 minutes of stable link, not a millisecond sooner
    p::State s;
    p::on_c6_restart(s, p::kResetBrownout, 1000);
    p::on_c6_restart(s, p::kResetBrownout, 2000);   // 11 dBm
    CHECK(!p::service(s, true, 10000));               // starts counting
    CHECK(!p::service(s, true, 10000 + p::kStepUpMs - 1) && p::cap_qdbm(s) == 44);
    CHECK(p::service(s, true, 10000 + p::kStepUpMs) && p::cap_qdbm(s) == 60);
    CHECK(!p::service(s, true, 10000 + p::kStepUpMs + 1000));
    CHECK(p::service(s, true, 10000 + 2 * p::kStepUpMs) && p::cap_qdbm(s) == 0);
    CHECK(!p::service(s, true, 10000 + 5 * p::kStepUpMs) && p::cap_qdbm(s) == 0);
  }
  {  // a link drop restarts the stable period; so does another supply restart
    p::State s;
    p::on_c6_restart(s, p::kResetBrownout, 1000);
    p::service(s, true, 2000);
    p::service(s, false, 2000 + p::kStepUpMs - 10);   // dropped just before the step up
    CHECK(!p::service(s, true, 2000 + p::kStepUpMs + 10) && p::cap_qdbm(s) == 60);
    p::on_c6_restart(s, p::kResetPowerOn, 2000 + p::kStepUpMs + 20);
    CHECK(p::cap_qdbm(s) == 44 && s.stable_since_ms == 0);
  }
  {  // a failure soon after a step up doubles the wait before the next one (10, 20, 40 min ... 2 h); a failure long after
     // a step up does not; a long stable spell at full power forgets the back-off
    p::State s;
    p::on_link_failure(s, 1000);                          // 15 dBm
    p::service(s, true, 2000);
    CHECK(p::service(s, true, 2000 + p::kStepUpMs) && p::cap_qdbm(s) == 0);   // back to full power after 10 min
    const uint32_t up = 2000 + p::kStepUpMs;
    p::on_link_failure(s, up + 12000);                    // failed 12 s after the step up
    CHECK(p::cap_qdbm(s) == 60 && s.step_up_ms == 2 * p::kStepUpMs);
    p::service(s, true, up + 20000);
    CHECK(!p::service(s, true, up + 20000 + p::kStepUpMs) && p::cap_qdbm(s) == 60);     // 10 min no longer enough
    CHECK(p::service(s, true, up + 20000 + 2 * p::kStepUpMs) && p::cap_qdbm(s) == 0);   // 20 min is
    const uint32_t up2 = up + 20000 + 2 * p::kStepUpMs;
    p::on_link_failure(s, up2 + 5000);                    // fails again soon after: 40 min next
    CHECK(s.step_up_ms == 4 * p::kStepUpMs);
    for (int i = 0; i < 10; ++i) { s.last_step_up_ms = 1; p::on_link_failure(s, 2); }
    CHECK(s.step_up_ms == p::kStepUpMaxMs);               // capped at 2 hours
    p::State t;                                           // a failure long after a step up keeps the normal wait
    p::on_link_failure(t, 1000); p::service(t, true, 2000); p::service(t, true, 2000 + p::kStepUpMs);
    p::on_link_failure(t, 2000 + 2 * p::kStepUpMs);
    CHECK(t.step_up_ms == p::kStepUpMs);
    p::State u; u.step_up_ms = p::kStepUpMaxMs;           // full power, stable for 2 h: back-off forgotten
    p::service(u, true, 1000);
    CHECK(!p::service(u, true, 1000 + p::kStepUpMaxMs) && u.step_up_ms == p::kStepUpMs);
  }
  {  // the clock wrapping round does not stop the step up
    p::State s;
    p::on_c6_restart(s, p::kResetBrownout, 1);
    p::service(s, true, 0xFFFFFF00u);
    CHECK(p::service(s, true, 0xFFFFFF00u + p::kStepUpMs) && p::cap_qdbm(s) == 0);
  }
  if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
  std::puts("PASS c6_tx_policy");
  return 0;
}
