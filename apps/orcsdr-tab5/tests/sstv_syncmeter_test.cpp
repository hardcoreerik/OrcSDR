// The decoder measures where its sync pulses really sit in the audio (nominal 1200 Hz) for the TUNE tab's tuning-error
// readout, and hands the audio it consumes to an observer for the sync-comb display.
#include "sstv_receiver.hpp"
#include "sstv_test_signal.hpp"
#include "sstv_tune.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace orcsdr::sstv;
using sstv_test::require;
using sstv_test::Signal;

namespace {
size_t g_observed = 0;
double g_observed_peak = 0;
void observer(const int16_t* samples, size_t count) {
  g_observed += count;
  for (size_t i = 0; i < count; ++i) g_observed_peak = std::max(g_observed_peak, std::fabs(static_cast<double>(samples[i])));
}

// A few Robot 36 lines: sync, porch, luma, chroma, as in the production signal, with the audio shifted by `offset`.
float measure(double offset, unsigned rows = 20, bool* locked = nullptr) {
  stop();
  clear_gallery();
  set_mode(Mode::auto_vis);
  set_auto_strategy(AutoStrategy::vis_sync);
  start(145500000);
  Signal sig;
  sig.tone_offset_hz = offset;
  sig.header(8);
  sig.robot36_rows(0, rows);
  sig.flush();
  const Metrics m = metrics();
  if (locked) *locked = m.sync_tone_pulses >= 4;
  const float tone = m.sync_tone_hz;
  stop();
  clear_gallery();
  return tone;
}
}  // namespace

int main() {
  // On frequency, the sync averages 1200 Hz to within the estimator's resolution.
  bool locked = false;
  const float on = measure(0, 20, &locked);
  require(locked, "syncmeter: pulses were averaged");
  std::fprintf(stderr, "sync tone on frequency: %.1f Hz\n", on);
  require(std::fabs(on - 1200.0f) < 12.0f, "syncmeter: on frequency the sync reads 1200 Hz");
  // A dial 60 Hz off shifts every tone by 60 Hz; the reading follows within about 20 Hz, in both directions.
  for (double offset : {-100.0, -60.0, 60.0, 100.0}) {
    const float tone = measure(offset);
    std::fprintf(stderr, "sync tone offset %+.0f Hz: %.1f Hz\n", offset, tone);
    require(std::fabs((tone - on) - offset) < 20.0f, "syncmeter: the reading follows the dial offset");
  }
  // The correction the TUNE tab shows has the right sign for USB and LSB.
  const float high = measure(60.0);
  const auto usb = orcsdr::sstv_tune::tuning_error(orcsdr::sstv_tune::Demod::usb, high, 8);
  const auto lsb = orcsdr::sstv_tune::tuning_error(orcsdr::sstv_tune::Demod::lsb, high, 8);
  require(usb.valid && usb.correction_hz > 40 && usb.correction_hz < 80, "syncmeter: USB reading 60 Hz high says raise the dial about 60 Hz");
  require(lsb.valid && lsb.correction_hz < -40 && lsb.correction_hz > -80, "syncmeter: LSB reading 60 Hz high says lower the dial");
  // The pixel tones shifted by the same offset still decode (brightness moves, structure does not).
  stop();
  clear_gallery();
  set_mode(Mode::auto_vis);
  start(145500000);
  Signal shifted;
  shifted.tone_offset_hz = 60;
  shifted.header(8);
  shifted.robot36_rows();
  shifted.silence(400);
  shifted.flush();
  require(archive_count() == 1 && archived_image(0).decoded_rows == 240, "syncmeter: a 60 Hz mistuned picture still decodes completely");
  stop();
  clear_gallery();
  // No pulses for five seconds clears the reading.
  start(145500000);
  Signal quiet_after;
  quiet_after.header(8);
  quiet_after.robot36_rows(0, 20);
  quiet_after.silence(6000);
  quiet_after.flush();
  require(metrics().sync_tone_hz == 0.0f && metrics().sync_tone_pulses == 0, "syncmeter: the reading expires when the pulses stop");
  stop();
  clear_gallery();
  // The observer sees every decoder-rate sample the receiver consumes.
  g_observed = 0;
  set_audio_observer(observer);
  start(145500000);
  Signal tone_signal;
  tone_signal.tone(1500, 1000);
  tone_signal.flush();
  set_audio_observer(nullptr);
  const double expected = 12000.0 * 1.0;
  require(std::fabs(static_cast<double>(g_observed) - expected) <= 200.0, "syncmeter: the observer receives the decoder-rate audio");
  require(g_observed_peak > 5000.0, "syncmeter: the observer sees real signal, not silence");
  stop();
  std::puts("SSTV sync meter: PASS");
  return 0;
}
