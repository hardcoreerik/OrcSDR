// Host tests for the TUNE tab's logic: VFO steps and touch tuning, the tuning-error direction on USB and LSB, the palette,
// and the audio scope (the "sync comb" spectrogram of the decoder's audio).
#include "sstv_audio_scope.hpp"
#include "sstv_tune.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace orcsdr;

namespace {
void require(bool pass, const char* why) {
  if (!pass) {
    std::fprintf(stderr, "SSTV FAIL: %s\n", why);
    std::exit(1);
  }
}

void test_vfo() {
  using sstv_tune::Demod;
  require(sstv_tune::default_demod(3845000) == Demod::lsb && sstv_tune::default_demod(7171000) == Demod::lsb, "tune: LSB below 10 MHz");
  require(sstv_tune::default_demod(14230000) == Demod::usb && sstv_tune::default_demod(28680000) == Demod::usb, "tune: USB from 10 to 30 MHz");
  require(sstv_tune::default_demod(145500000) == Demod::nfm && sstv_tune::default_demod(437550000) == Demod::nfm, "tune: FM above 30 MHz");
  require(sstv_tune::nudge(14230000, 1000, 1) == 14231000 && sstv_tune::nudge(14230000, 1000, -1) == 14229000, "tune: nudge on the grid");
  require(sstv_tune::nudge(4294967200u, 100, 1) == sstv_tune::clamp_hz(0xFFFFFFFFu) &&
          sstv_tune::from_touch(3000000000u, 3000000000u, 999, 1000, 1) == sstv_tune::clamp_hz(0xFFFFFFFFu),
          "tune: a result above 32 bits saturates at the top of the range instead of wrapping");
  require(sstv_tune::nudge(14230400, 1000, 1) == 14231000 && sstv_tune::nudge(14230400, 1000, -1) == 14230000,
          "tune: nudge from off the grid lands on the next multiple, not past it");
  uint32_t f = 14230400;
  for (int i = 0; i < 5; ++i) f = sstv_tune::nudge(f, 100, 1);
  require(f == 14230900, "tune: five 100 Hz steps from an off-grid start stay on the grid");
  require(sstv_tune::nudge(sstv_tune::kMinHz, 1000, -1) == sstv_tune::kMinHz && sstv_tune::nudge(sstv_tune::kMaxHz, 1000, 1) == sstv_tune::kMaxHz,
          "tune: clamped to the receiver's range");
  require(sstv_tune::step_hz(sstv_tune::step_index(500)) == 500 && sstv_tune::step_hz(sstv_tune::step_index(777)) == 1000, "tune: step list lookup");
  require(sstv_tune::default_step(Demod::nfm) == 12500 && sstv_tune::default_step(Demod::usb) == 100, "tune: default steps");
  // Tap on a 600-pixel plot spanning 240 kHz centred on 145.5 MHz.
  require(sstv_tune::from_touch(145500000, 240000, 300, 600, 1000) == 145500000, "tune: centre tap keeps the centre");
  require(sstv_tune::from_touch(145500000, 240000, 0, 600, 1000) == 145380000, "tune: left edge is half a span down");
  require(sstv_tune::from_touch(145500000, 240000, 599, 600, 1000) == 145620000, "tune: right edge rounds to the step");
  require(sstv_tune::from_touch(145500000, 240000, 330, 600, 1000) == 145512000, "tune: 30 px right is 12 kHz up");
  for (uint32_t hz : {145390000u, 145500000u, 145600000u}) {
    const int x = sstv_tune::to_x(145500000, 240000, hz, 600);
    require(std::abs(static_cast<int>(sstv_tune::from_touch(145500000, 240000, x, 600, 1)) - static_cast<int>(hz)) <= 400,
            "tune: to_x and from_touch agree to within one pixel");
  }
  require(sstv_tune::to_x(145500000, 240000, 100000000, 600) == 0 && sstv_tune::to_x(145500000, 240000, 200000000, 600) == 599,
          "tune: frequencies outside the plot clamp to its edges");
}

void test_tuning_error() {
  using sstv_tune::Demod;
  auto usb = sstv_tune::tuning_error(Demod::usb, 1250.0f, 10);
  require(usb.valid && usb.correction_hz == 50, "error: sync reads 50 Hz high on USB -> raise the dial by 50 Hz");
  auto lsb = sstv_tune::tuning_error(Demod::lsb, 1250.0f, 10);
  require(lsb.valid && lsb.correction_hz == -50, "error: the same reading on LSB means lower the dial");
  require(sstv_tune::tuning_error(Demod::usb, 1150.0f, 10).correction_hz == -50, "error: low reading on USB");
  require(!sstv_tune::tuning_error(Demod::nfm, 1250.0f, 10).valid, "error: no correction applies in FM");
  require(!sstv_tune::tuning_error(Demod::usb, 1250.0f, 2).valid, "error: too few pulses is not trusted");
  require(!sstv_tune::tuning_error(Demod::usb, 300.0f, 10).valid && !sstv_tune::tuning_error(Demod::usb, 3000.0f, 10).valid,
          "error: an impossible sync tone is rejected");
}

void test_palette() {
  require(sstv_tune::palette565(0) != sstv_tune::palette565(255), "palette: ends differ");
  int changes = 0;
  uint16_t last = sstv_tune::palette565(0);
  for (int i = 1; i < 256; ++i) {
    const uint16_t c = sstv_tune::palette565(static_cast<uint8_t>(i));
    if (c != last) ++changes;
    last = c;
  }
  require(changes > 100, "palette: a smooth ramp, not a few flat bands");
  require(sstv_tune::tint565(sstv_tune::Tint::searching) != sstv_tune::tint565(sstv_tune::Tint::receiving) &&
          sstv_tune::tint565(sstv_tune::Tint::receiving) != sstv_tune::tint565(sstv_tune::Tint::recovering), "palette: state tints differ");
  require(sstv_tune::self_check(), "tune: self check");
}

std::vector<int16_t> tone(double hz, size_t samples, double amplitude = 11000) {
  std::vector<int16_t> out(samples);
  double p = 0.0;
  for (size_t i = 0; i < samples; ++i) {
    out[i] = static_cast<int16_t>(std::lround(amplitude * std::sin(p)));
    p += 2.0 * M_PI * hz / sstv_audio_scope::kSampleRate;
  }
  return out;
}

const uint8_t* newest_row(const sstv_audio_scope::View& v, size_t age = 0) {
  return v.rows + ((v.sequence - 1 - age) % v.row_count) * v.bins;
}

size_t peak_bin(const sstv_audio_scope::View& v, size_t age) {
  const uint8_t* row = newest_row(v, age);
  size_t best = 0;
  for (size_t b = 1; b < v.bins; ++b)
    if (row[b] > row[best]) best = b;
  return best;
}

void test_audio_scope() {
  namespace as = sstv_audio_scope;
  require(!as::active() && as::view().rows == nullptr, "scope: nothing allocated before start()");
  require(as::start() && as::active(), "scope: starts");
  as::push(nullptr, 100);   // ignored
  for (double hz : {1200.0, 1500.0, 1900.0, 2300.0}) {
    const auto samples = tone(hz, as::kFft * 3);
    as::push(samples.data(), samples.size());
    const auto v = as::view();
    const size_t bin = peak_bin(v, 0);
    require(std::abs(static_cast<int>(as::bin_hz(bin)) - static_cast<int>(hz)) <= 24, "scope: the peak bin sits on the tone");
    const uint8_t* row = newest_row(v);
    require(row[bin] > 200, "scope: a strong tone reads near the top of the scale");
    const size_t far = (bin + 30) % v.bins;
    require(row[far] < 60, "scope: bins far from the tone stay dark");
  }
  const uint32_t before = as::view().sequence;
  const std::vector<int16_t> quiet(as::kFft, 0);
  as::push(quiet.data(), quiet.size());
  require(as::view().sequence == before + 1, "scope: exactly one row per 512 samples");
  {
    const auto v = as::view();
    const uint8_t* row = newest_row(v);
    size_t lit = 0;
    for (size_t b = 0; b < v.bins; ++b) lit += row[b] > 0;
    require(lit == 0, "scope: silence is all dark");
  }
  // Odd chunk sizes give the same number of rows as one big push.
  const uint32_t start_seq = as::view().sequence;
  const auto chirp = tone(1500, as::kFft * 4 + 77);
  size_t offset = 0, chunk = 1;
  while (offset < chirp.size()) {
    const size_t n = std::min(chunk, chirp.size() - offset);
    as::push(chirp.data() + offset, n);
    offset += n;
    chunk = chunk * 3 + 1;
  }
  require(as::view().sequence == start_seq + 4, "scope: chunked pushes produce the same rows");
  // A tone that steps between two frequencies shows both in the history, newest row first.
  const auto a = tone(1500, as::kFft), b = tone(2300, as::kFft);
  as::push(a.data(), a.size());
  as::push(b.data(), b.size());
  const auto h = as::view();
  require(std::abs(static_cast<int>(as::bin_hz(peak_bin(h, 0))) - 2300) <= 24 && std::abs(static_cast<int>(as::bin_hz(peak_bin(h, 1))) - 1500) <= 24,
          "scope: history keeps the earlier row");
  require(as::self_check(), "scope: self check");
  as::stop();
  require(!as::active(), "scope: stops");
}

void test_entry() {
  uint32_t hz = 0;
  require(sstv_tune::parse_entry("145.5", &hz) && hz == 145500000, "entry: decimal is MHz");
  require(sstv_tune::parse_entry("14.230", &hz) && hz == 14230000, "entry: HF in MHz");
  require(sstv_tune::parse_entry("14230", &hz) && hz == 14230000, "entry: five digits are kHz");
  require(sstv_tune::parse_entry("145500", &hz) && hz == 145500000, "entry: six digits are kHz");
  require(sstv_tune::parse_entry("145", &hz) && hz == 145000000, "entry: short whole number is MHz");
  require(sstv_tune::parse_entry("437.55", &hz) && hz == 437550000, "entry: UHF");
  require(sstv_tune::parse_entry("0.1", &hz) && hz == 100000, "entry: lowest tunable");
  require(!sstv_tune::parse_entry("", &hz) && !sstv_tune::parse_entry(".", &hz) && !sstv_tune::parse_entry("1.2.3", &hz), "entry: malformed refused");
  require(!sstv_tune::parse_entry("0.05", &hz) && !sstv_tune::parse_entry("1800", &hz) && !sstv_tune::parse_entry("2000000", &hz),
          "entry: out of range refused");
  char text[16];
  sstv_tune::format_mhz(text, sizeof(text), 145500000);
  require(std::strcmp(text, "145.50000") == 0, "entry: readout format");
  sstv_tune::format_mhz(text, sizeof(text), 14230250);
  require(std::strcmp(text, "14.23025") == 0, "entry: readout keeps 10 Hz");
}
}  // namespace

int main() {
  test_entry();
  test_vfo();
  test_tuning_error();
  test_palette();
  test_audio_scope();
  std::puts("SSTV tune: PASS");
  return 0;
}
