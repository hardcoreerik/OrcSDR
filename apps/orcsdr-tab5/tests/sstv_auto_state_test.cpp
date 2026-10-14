// Host regression for the SSTV Auto session lifecycle: rollover, dropped audio,
// silence handling and (later) multi-mode transitions. Complements the legacy
// smoke test, which must keep passing unchanged.
// Build: see docs/sstv/README.md (needs -DORCSDR_SSTV_TESTING).
#include "sstv_receiver.hpp"
#include "sstv_test_signal.hpp"
#include <cstdio>
#include <cstdlib>

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

uint16_t gray565(uint8_t v) {
  return static_cast<uint16_t>(((v & 248) << 8) | ((v & 252) << 3) | (v >> 3));
}
uint8_t luma_of(double hz) {
  int v = static_cast<int>((hz - 1500.) * 255. / 800. + .5);
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Per-reception counters belong to one image, not to the receiver lifetime.
void test_per_session_counters() {
  reset_receiver();
  Signal sig;
  uint32_t first_session = 0;
  for (int round = 0; round < 2; ++round) {
    sig.header(8);
    sig.robot36_rows();
    sig.tone(1900, 50);
    sig.flush();
    const auto image = archived_image(0);
    require(image.session_id != 0 && image.session_id != first_session,
            "session: every reception has a new, non-zero session id");
    first_session = image.session_id;
    require(image.vis_code == 8 && image.identification == Identification::vis,
            "session: VIS-identified image records its VIS code");
    require(image.sync_locked >= 235 && image.sync_locked <= 241,
            "session: sync_locked counts this image only (not lifetime)");
    require(image.sync_missed <= 2, "session: sync_missed counts this image only");
    sig.silence(500);
  }
  require(metrics().sync_locked_lines > 400, "session: lifetime counter keeps accumulating");
  stop();
  clear_gallery();
}

// Robot 36 -> PD120 -> Scottie S1 without any operator action in between.
void test_multi_mode_sequence() {
  reset_receiver();
  const auto overruns_before = metrics().audio_overruns;  // lifetime counter
  Signal sig;
  sig.header(8);
  sig.robot36_rows();
  sig.tone(1900, 50);
  sig.silence(2000);
  sig.header(95);
  sig.pd120_pairs();
  sig.tone(1900, 50);
  sig.silence(2000);
  sig.header(60);
  sig.tone(1200, 9);  // Scottie's extra post-VIS sync pulse
  sig.scottie_s1_rows();
  sig.tone(1900, 50);
  sig.silence(500);
  sig.flush();
  require(archive_count() == 3, "sequence: three transmissions, three archived images");
  const Mode expect[3] = {Mode::scottie_s1, Mode::pd120, Mode::robot36};
  const uint16_t rows[3] = {256, 496, 240};
  for (size_t i = 0; i < 3; ++i) {
    const auto image = archived_image(i);
    require(image.mode == expect[i], "sequence: modes identified in order");
    require(image.decoded_rows == rows[i] && !image.partial, "sequence: each image complete");
    require(image.identification == Identification::vis, "sequence: each identified by VIS");
    if (i + 1 < 3)
      require(image.session_id > archived_image(i + 1).session_id,
              "sequence: session ids increase");
  }
  require(!metrics().receiving, "sequence: receiver returned to listening");
  require(metrics().audio_overruns == overruns_before, "sequence: no new ring overruns");
  stop();
  clear_gallery();
}

void test_noise_makes_no_image() {
  reset_receiver();
  const auto before = metrics();
  Signal sig;
  sig.noise(20000, 8000);
  sig.noise(5000, 30000);
  sig.tone(1200, 30000);  // an unrelated, continuous 1200 Hz carrier
  sig.flush();
  const auto m = metrics();
  require(archive_count() == 0 && !m.receiving, "noise: no image from noise or a steady 1200 Hz tone");
  require(m.good_vis == before.good_vis, "noise: no VIS accepted");
  require(m.auto_sync_locks == before.auto_sync_locks, "noise: no sync lock");
  stop();
  clear_gallery();
}

// Pulse trains that are irregular, or regular at a period no mode uses, must not lock.
void test_false_pulse_trains_do_not_lock() {
  reset_receiver();
  const auto before = metrics();
  Signal sig;
  uint32_t r = 99u;
  for (unsigned i = 0; i < 60; ++i) {  // irregular spacing 80..700 ms
    r = r * 1664525u + 1013904223u;
    sig.tone(1200, 9);
    sig.tone(1900, 80. + static_cast<double>((r >> 16) % 620u));
  }
  for (unsigned i = 0; i < 30; ++i) {  // regular 240 ms: between Robot 36 and Robot 72
    sig.tone(1200, 9);
    sig.tone(1900, 231);
  }
  for (unsigned i = 0; i < 30; ++i) {  // regular 137 ms: near Robot 36 but 8.7% off
    sig.tone(1200, 9);
    sig.tone(1900, 128);
  }
  sig.flush();
  require(metrics().auto_sync_locks == before.auto_sync_locks && archive_count() == 0,
          "false pulses: irregular or unmatched periods never lock a mode");
  stop();
  clear_gallery();
}

// A damaged VIS must not be trusted; the sync fallback may still identify the
// mode but must say so, and the image must stay partial.
void test_damaged_vis_falls_back_honestly() {
  reset_receiver();
  const auto before = metrics();
  Signal sig;
  sig.preamble();
  sig.vis(8, /*flip_parity=*/true);
  sig.robot36_rows();
  sig.tone(1900, 1700);   // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  sig.flush();
  const auto m = metrics();
  require(m.good_vis == before.good_vis && m.bad_vis > before.bad_vis,
          "damaged VIS: parity error counted, header rejected");
  require(archive_count() == 1, "damaged VIS: sync recovery still produces one image");
  const auto image = archived_image(0);
  require(image.mode == Mode::robot36 && image.identification == Identification::sync,
          "damaged VIS: identified by sync, never labelled VIS");
  require(image.partial && image.start_unknown && image.vis_code == 0,
          "damaged VIS: partial, unknown start, no VIS code");
  stop();
  clear_gallery();
}

void test_mid_transmission_join() {
  reset_receiver();
  Signal sig;
  sig.robot36_rows(80, 160);  // tuned in at row 80, no header
  sig.tone(1900, 1700);   // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  sig.flush();
  require(archive_count() == 1, "join: late-joined transmission preserved");
  const auto image = archived_image(0);
  require(image.identification == Identification::sync && image.partial && image.start_unknown,
          "join: sync-identified, partial, origin unknown");
  require(image.decoded_rows >= 140 && image.decoded_rows <= 160,
          "join: roughly the 160 transmitted rows, none invented");
  stop();
  clear_gallery();
}

void test_retune_abort_and_loss() {
  // Retune keeps the partial image and keeps listening.
  reset_receiver();
  Signal sig;
  sig.header(8);
  sig.robot36_rows(0, 60);
  sig.flush();
  set_frequency(145800000u + 1000u);
  require(archive_count() == 1 && archived_image(0).end_reason == EndReason::retune &&
              archived_image(0).partial,
          "retune: partial image preserved with reason");
  require(!metrics().receiving && active(), "retune: receiver keeps listening");
  sig.silence(500);
  sig.header(8);
  sig.robot36_rows();
  sig.tone(1900, 50);
  sig.flush();
  require(archive_count() == 2 && !archived_image(0).partial, "retune: next transmission decodes");

  // User abort (stop) preserves the partial image.
  reset_receiver();
  Signal b;
  b.header(8);
  b.robot36_rows(0, 50);
  b.flush();
  stop();
  require(archive_count() == 1 && archived_image(0).end_reason == EndReason::aborted,
          "abort: partial preserved with reason aborted");
  clear_gallery();

  // Prolonged loss of sync archives the partial image and recovers by itself.
  reset_receiver();
  Signal c;
  c.header(8);
  c.robot36_rows(0, 100);
  c.silence(3000);
  c.flush();
  require(archive_count() == 1 && archived_image(0).end_reason == EndReason::sync_lost &&
              archived_image(0).partial,
          "loss: prolonged silence ends the image as sync_lost, partial");
  require(archived_image(0).decoded_rows >= 100 && archived_image(0).decoded_rows <= 112,
          "loss: bounded holdover, no invented rows");
  c.header(8);
  c.robot36_rows();
  c.tone(1900, 50);
  c.flush();
  require(archive_count() == 2 && archived_image(0).decoded_rows == 240 &&
              !archived_image(0).partial,
          "loss: recovers and decodes the next image");
  stop();
  clear_gallery();
}

void test_fade_and_clock_drift() {
  // A 300 ms fade (two lines) is bridged from the predicted timeline.
  reset_receiver();
  Signal sig;
  sig.header(8);
  sig.robot36_rows(0, 100);
  sig.silence(300);
  sig.robot36_rows(102, 138);
  sig.tone(1900, 50);
  sig.flush();
  require(archive_count() == 1, "fade: image survives a 2-line fade");
  auto image = archived_image(0);
  require(image.decoded_rows == 240 && !image.partial, "fade: completes all 240 rows");
  require(image.sync_missed >= 1 && image.sync_missed <= 3,
          "fade: the missed lines are recorded for this image");
  stop();
  clear_gallery();

  // Transmitter clock error up to +-1 percent.
  const double scales[3] = {1.003, 0.997, 1.010};
  for (double scale : scales) {
    reset_receiver();
    Signal d;
    d.header(8);
    d.time_scale = scale;
    d.robot36_rows();
    d.time_scale = 1.0;
    d.tone(1900, 100);
    d.flush();
    require(archive_count() == 1 && archived_image(0).decoded_rows == 240,
            "drift: Robot 36 completes with clock error");
    require(archived_image(0).sync_locked >= 230, "drift: line sync keeps locking");
    stop();
    clear_gallery();
  }
}

// Eviction must never hand out or free an image another entry still owns.
void test_gallery_ownership() {
  reset_receiver();
  Signal sig;
  const double hz[6] = {1600, 1700, 1800, 1900, 2000, 2100};
  for (int k = 0; k < 6; ++k) {
    sig.luma_hz = hz[k];
    sig.header(8);
    sig.robot36_rows();
    sig.tone(1900, 50);
    sig.silence(300);
    sig.flush();
  }
  require(archive_count() == 4, "gallery: capped at four entries");
  for (size_t i = 0; i < 4; ++i) {
    const auto image = archived_image(i);
    require(image.pixels != nullptr, "gallery: every entry owns pixels");
    const double expect_hz = hz[5 - i];
    const uint16_t px = image.pixels[50u * 320u + 160u];
    const uint16_t want = gray565(luma_of(expect_hz));
    const int dg = std::abs(static_cast<int>((px >> 5) & 63) - static_cast<int>((want >> 5) & 63));
    require(dg <= 3, "gallery: entry holds its own image content after eviction");
    for (size_t j = i + 1; j < 4; ++j)
      require(image.pixels != archived_image(j).pixels, "gallery: no shared framebuffers");
  }
  stop();
  clear_gallery();
}

// A pinned image (what the SD writer holds) must outlive gallery eviction, and
// every frame must be freed exactly once.
void test_pinned_frame_outlives_eviction() {
  reset_receiver();
  Signal sig;
  sig.luma_hz = 2000;
  sig.header(8);
  sig.robot36_rows();
  sig.tone(1900, 50);
  sig.silence(300);
  sig.flush();
  require(archive_count() == 1, "pin: image archived");
  const Image pinned = pin_archived(0);
  require(pinned.pixels != nullptr && pinned.frame != nullptr, "pin: pin returns a live image");
  const uint16_t want = gray565(luma_of(2000));
  for (int k = 0; k < 5; ++k) {  // five newer images evict the pinned one from the gallery
    sig.luma_hz = 1600 + 80. * k;
    sig.header(8);
    sig.robot36_rows();
    sig.tone(1900, 50);
    sig.silence(300);
    sig.flush();
  }
  require(archive_count() == 4, "pin: gallery stays capped");
  for (size_t i = 0; i < archive_count(); ++i)
    require(archived_image(i).pixels != pinned.pixels, "pin: evicted image is no longer in the gallery");
  const uint16_t px = pinned.pixels[50u * 320u + 160u];
  require(std::abs(static_cast<int>((px >> 5) & 63) - static_cast<int>((want >> 5) & 63)) <= 3,
          "pin: pinned pixels still hold the original content after eviction");
  require(metrics().frames_alive == 5, "pin: four gallery frames plus the pinned one");
  release_image(pinned);
  require(metrics().frames_alive == 4, "pin: releasing the last reference frees exactly that frame");
  clear_gallery();
  require(metrics().frames_alive == 0 && metrics().frame_bytes == 0, "pin: nothing leaks");
  stop();
}

unsigned g_seen = 0;
void note_state() { g_seen |= 1u << static_cast<unsigned>(session_state()); }
bool seen(SessionState state) { return (g_seen >> static_cast<unsigned>(state)) & 1u; }

// The lifecycle the dashboard shows, from one source of truth.
void test_session_state_machine() {
  // VIS path: searching -> VIS acquisition -> receiving -> loss recovery -> finalizing -> ready -> searching
  reset_receiver();
  g_seen = 0;
  require(session_state() == SessionState::searching, "state: starts searching");
  Signal sig;
  sig.preamble();
  note_state();
  require(session_state() == SessionState::vis_acquisition, "state: a VIS leader is being validated");
  sig.vis(8);
  for (unsigned row = 0; row < 40; ++row) { sig.robot36_rows(row, 1); note_state(); }
  require(session_state() == SessionState::receiving && metrics().state == SessionState::receiving,
          "state: VIS-identified image is receiving");
  sig.silence(400);                     // two lines of fade
  note_state();
  require(seen(SessionState::loss_recovery), "state: a fade shows loss recovery");
  for (unsigned i = 0; i < 80; ++i) { sig.silence(100); note_state(); }
  require(seen(SessionState::finalizing) && seen(SessionState::ready),
          "state: an ended image passes through finalizing and ready");
  require(session_state() == SessionState::searching, "state: returns to searching on its own");

  // Sync-only path: analysis -> provisional lock -> receiving (confirmed by further lines).
  reset_receiver();
  g_seen = 0;
  Signal join;
  for (unsigned row = 40; row < 100; ++row) { join.robot36_rows(row, 1); note_state(); }
  require(seen(SessionState::sync_analysis), "state: repeated pulses show sync analysis");
  require(seen(SessionState::provisional_lock), "state: a sync-only mode choice is provisional first");
  require(session_state() == SessionState::receiving, "state: confirmed by further locked lines");
  require(!seen(SessionState::vis_acquisition), "state: no VIS stage without a VIS leader");
  stop();
  clear_gallery();
}

void test_service_cost_is_measured() {
  reset_receiver();
  Signal sig;
  sig.header(8);
  sig.robot36_rows(0, 60);
  sig.flush();
  const auto m = metrics();
  std::fprintf(stderr, "perf (host, not the P4): service last=%lu us max=%lu us avg=%lu us cpu=%u%% ring max=%u/4096\n",
               static_cast<unsigned long>(m.service_us_last), static_cast<unsigned long>(m.service_us_max),
               static_cast<unsigned long>(m.service_us_avg), static_cast<unsigned>(m.service_cpu_pct),
               static_cast<unsigned>(m.ring_fill_max));
  require(m.service_us_max > 0 && m.ring_fill_max > 0, "perf: service time and ring fill are measured");
  require(m.ring_fill_max < 4096, "perf: the PCM ring never filled");
  stop();
  clear_gallery();
}

uint32_t fake_utc() { return 1700000123u; }

// Timestamps and the manual-capture snapshot of an image still being received.
void test_timestamp_and_live_snapshot() {
  reset_receiver();
  Signal sig;
  sig.header(8);
  sig.robot36_rows(0, 40);
  sig.tone(1900, 5);
  sig.flush();
  set_utc_source(fake_utc);
  const Image snap = snapshot_live();
  require(snap.pixels != nullptr && snap.frame != nullptr, "snapshot: copy of the live image");
  require(snap.partial && snap.end_reason == EndReason::captured && snap.serial == 0,
          "snapshot: marked partial, captured, not a gallery entry");
  require(snap.decoded_rows >= 36 && snap.decoded_rows <= 41, "snapshot: rows decoded so far");
  require(snap.utc_epoch == 1700000123u && snap.vis_code == 8, "snapshot: timestamp and VIS code");
  require(metrics().frames_alive == 2, "snapshot: live frame plus the independent copy");
  const uint16_t before = snap.pixels[10u * 320u + 100u];
  sig.robot36_rows(40, 80);   // the live image keeps changing ...
  sig.flush();
  require(snap.pixels[10u * 320u + 100u] == before && snap.pixels[100u * 320u + 100u] == 0,
          "snapshot: ... but the copy is frozen");
  release_image(snap);
  sig.robot36_rows(120, 120);
  sig.tone(1900, 50);
  sig.flush();
  require(archive_count() == 1 && archived_image(0).utc_epoch == 1700000123u,
          "timestamp: a finished reception carries the verified UTC");
  require(metrics().frames_alive == 1, "snapshot: released copy freed");
  set_utc_source(nullptr);
  stop();
  clear_gallery();
  require(snapshot_live().pixels == nullptr, "snapshot: nothing to copy when idle");
}

void test_unknown_and_manual_vis() {
  reset_receiver();
  Signal sig;
  sig.header(127);  // valid parity, no such mode
  sig.silence(500);
  sig.flush();
  require(archive_count() == 0 && !metrics().receiving, "unknown VIS: no image");
  // Manual selection is a VIS match filter: a different mode's header is ignored.
  set_mode(Mode::pd120);
  sig.header(8);
  sig.robot36_rows(0, 40);
  sig.silence(500);
  sig.flush();
  require(archive_count() == 0, "manual: non-matching VIS ignored");
  sig.header(95);
  sig.pd120_pairs(0, 12);
  sig.flush();
  require(metrics().receiving && metrics().received_mode == Mode::pd120,
          "manual: matching VIS starts the selected mode");
  set_mode(Mode::auto_vis);
  stop();
  clear_gallery();
}

// A valid new VIS outranks a speculative sync lock.
void test_vis_preempts_sync_lock() {
  reset_receiver();
  Signal sig;
  sig.robot36_rows(40, 30);  // join without header: sync lock after ~6 lines
  sig.flush();
  require(metrics().receiving && metrics().identification == Identification::sync,
          "preempt: speculative sync lock active");
  sig.header(95);
  sig.pd120_pairs(0, 8);
  sig.flush();
  require(archive_count() == 1, "preempt: sync-identified partial archived");
  const auto old = archived_image(0);
  require(old.identification == Identification::sync && old.end_reason == EndReason::new_vis &&
              old.partial,
          "preempt: speculative image kept as partial, reason new_vis");
  require(metrics().receiving && metrics().identification == Identification::vis &&
              metrics().received_mode == Mode::pd120,
          "preempt: VIS-identified PD120 now receiving");
  stop();
  clear_gallery();
}

}  // namespace

int main() {
  require(self_check(), "static receiver invariants");
  test_tick_rollover();
  test_overrun_keeps_timeline();
  test_silence_between_syncs();
  test_per_session_counters();
  test_multi_mode_sequence();
  test_noise_makes_no_image();
  test_false_pulse_trains_do_not_lock();
  test_damaged_vis_falls_back_honestly();
  test_mid_transmission_join();
  test_retune_abort_and_loss();
  test_fade_and_clock_drift();
  test_gallery_ownership();
  test_pinned_frame_outlives_eviction();
  test_session_state_machine();
  test_timestamp_and_live_snapshot();
  test_service_cost_is_measured();
  test_unknown_and_manual_vis();
  test_vis_preempts_sync_lock();
  std::puts("SSTV auto state: PASS");
  return 0;
}
