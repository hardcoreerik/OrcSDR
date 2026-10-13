// Host regression for the SSTV Auto session lifecycle: rollover, dropped audio,
// silence handling and (later) multi-mode transitions. Complements the legacy
// smoke test, which must keep passing unchanged.
// Build: see docs/sstv/README.md (needs -DORCSDR_SSTV_TESTING).
#include "sstv_receiver.hpp"
#include "sstv_test_signal.hpp"
#include <cstdio>

using namespace orcsdr::sstv;
using sstv_test::require;
using sstv_test::Signal;

namespace {

void reset_receiver(AutoStrategy strategy = AutoStrategy::vis_sync) {
  stop();
  clear_gallery();
  set_mode(Mode::auto_vis);
  set_auto_strategy(strategy);
  start(145800000);
}

// A complete Robot 36 transmission whose sample clock crosses the 32-bit wrap.
// The decoder runs at 12 kHz, so 2^32 ticks is ~99.4 hours; start 20 s before it.
void test_tick_rollover() {
  reset_receiver();
  testing::set_tick_base(0xFFFFFFFFull - 12000ull * 20ull);
  Signal sig;
  sig.header(8);
  sig.robot36_rows();
  sig.tone(1900, 50);
  sig.flush();
  const auto m = metrics();
  std::fprintf(stderr, "rollover: locked=%lu missed=%lu archive=%zu\n",
               static_cast<unsigned long>(m.sync_locked_lines),
               static_cast<unsigned long>(m.sync_missed_lines), archive_count());
  require(archive_count() == 1, "rollover: Robot36 archived across the 32-bit tick wrap");
  const auto image = archived_image(0);
  require(image.mode == Mode::robot36 && image.decoded_rows == 240,
          "rollover: all 240 rows decoded across the wrap");
  require(!image.partial, "rollover: image is complete, not partial");
  require(m.sync_locked_lines >= 235, "rollover: line sync stays locked across the wrap");
  require(m.sync_missed_lines <= 2, "rollover: no missed-sync burst at the wrap");
  stop();
  clear_gallery();
}

// Audio dropped in the ring (decoder starved) must not shift the raster
// timeline: the next line sync after the gap must still lock.
void test_overrun_keeps_timeline() {
  reset_receiver();
  Signal sig;
  sig.header(8);
  sig.robot36_rows(0, 100);
  sig.service_enabled = false;   // starve the decoder for 600 ms (4 rows)
  sig.robot36_rows(100, 4);
  sig.flush();
  sig.service_enabled = true;
  sig.robot36_rows(104, 136);
  sig.tone(1900, 50);
  sig.flush();
  const auto m = metrics();
  std::fprintf(stderr, "overrun: drops=%lu locked=%lu missed=%lu archive=%zu\n",
               static_cast<unsigned long>(m.audio_overruns),
               static_cast<unsigned long>(m.sync_locked_lines),
               static_cast<unsigned long>(m.sync_missed_lines), archive_count());
  require(m.audio_overruns > 0, "overrun: scenario really dropped samples");
  require(archive_count() == 1, "overrun: image archived once");
  const auto image = archived_image(0);
  require(image.mode == Mode::robot36 && image.decoded_rows == 240,
          "overrun: reception continues to row 240 after a 0.26 s audio gap");
  require(image.end_reason == EndReason::complete,
          "overrun: ends complete, not sync_lost");
  stop();
  clear_gallery();
}

// Silence between sync pulses must not stretch the measured pulse width.
void test_silence_between_syncs() {
  reset_receiver();
  const auto before = metrics();
  Signal sig;
  for (unsigned i = 0; i < 12; ++i) {
    sig.tone(1200, 9);
    sig.silence(141);   // 150 ms line period, nothing else on air
  }
  sig.silence(200);
  sig.flush();
  const auto m = metrics();
  const auto pulses = m.observed_sync_pulses - before.observed_sync_pulses;
  const auto locks = m.auto_sync_locks - before.auto_sync_locks;
  std::fprintf(stderr, "silence: pulses=%lu locks=%lu pulse=%.1f ms interval=%.1f ms\n",
               static_cast<unsigned long>(pulses), static_cast<unsigned long>(locks),
               static_cast<double>(m.sync_pulse_ms), static_cast<double>(m.sync_interval_ms));
  require(pulses >= 10, "silence: every 9 ms pulse observed");
  // The old decoder held the last tone through silence, so each pulse was
  // measured as the whole quiet gap (~150 ms) instead of ~9 ms.
  require(m.sync_pulse_ms > 6.f && m.sync_pulse_ms < 16.f,
          "silence: pulse width is not stretched by the following silence");
  require(m.sync_interval_ms > 140.f && m.sync_interval_ms < 160.f,
          "silence: line interval measures the 150 ms period");
  stop();
  clear_gallery();
}

}  // namespace

int main() {
  require(self_check(), "static receiver invariants");
  test_tick_rollover();
  test_overrun_keeps_timeline();
  test_silence_between_syncs();
  std::puts("SSTV auto state: PASS");
  return 0;
}
