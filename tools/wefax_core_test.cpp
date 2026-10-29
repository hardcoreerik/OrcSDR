// Standalone, independent synthetic FM encoder + deterministic regression tests.
// Build with tools/wefax_host_test.sh from the repository root.
#include "wefax_core.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <vector>
using orcsdr::wefax::Decoder;
using orcsdr::wefax::Format;
namespace {
constexpr double pi = 3.14159265358979323846;
struct Capture {
  uint16_t width = 0;
  std::vector<std::vector<uint8_t>> rows;
  static void row(void* self, uint32_t index, const uint8_t* pixels, uint16_t width) {
    auto* capture = static_cast<Capture*>(self);
    if (index != capture->rows.size()) std::abort();
    capture->width = width;
    capture->rows.emplace_back(pixels, pixels + width);
  }
};

// The independent encoder emits a continuous-phase, real FM audio subcarrier.
// The pattern is not copied from any existing weather-fax implementation.
uint8_t source_pixel(size_t row, size_t x, size_t width) {
  if (row % 5 == 0) return 0;
  if (row % 5 == 1) return 255;
  if (row % 5 == 2) return x < width / 2 ? 0 : 255;
  if (row % 5 == 3) return static_cast<uint8_t>((x * 255) / (width - 1));
  return ((x / 120) % 2 == 0) ? 255 : 0;
}
std::vector<int16_t> encode(int rate, int lpm, int width, int rows,
                            int amplitude = 22000, float snr_db = 99) {
  std::vector<int16_t> pcm;
  const int samples_per_row = rate * 60 / lpm;
  const int total = samples_per_row * rows;
  pcm.reserve(total);
  double phase = 0.0;
  std::mt19937 rng(0xC0FFEE);
  const double amplitude_noise = amplitude * std::pow(10.0, -snr_db / 20.0);
  std::normal_distribution<double> noise(0.0, amplitude_noise);
  for (int n = 0; n < total; ++n) {
    const int row = n / samples_per_row;
    const int sample_in_row = n % samples_per_row;
    const auto x = static_cast<size_t>((static_cast<uint64_t>(sample_in_row) * width) /
                                        samples_per_row);
    const auto brightness = source_pixel(row, x, width);
    const double freq = 1500.0 + (800.0 * brightness / 255.0);
    phase += 2 * pi * freq / rate;
    const double sample = amplitude * std::sin(phase) + noise(rng);
    pcm.push_back(static_cast<int16_t>(std::lround(std::max(-32760.0, std::min(32760.0, sample)))));
  }
  return pcm;
}
void send(Decoder& d, const std::vector<int16_t>& pcm, bool at_48k, int chunk) {
  for (size_t offset = 0; offset < pcm.size(); offset += chunk) {
    const size_t n = std::min(static_cast<size_t>(chunk), pcm.size() - offset);
    if (at_48k) d.process_pcm_48k(pcm.data() + offset, n);
    else d.process_pcm_12k(pcm.data() + offset, n);
  }
}
struct Score { double mae=0; size_t count=0; };
Score compare(const Capture& capture) {
  Score result;
  for (size_t row = 1; row < capture.rows.size(); ++row) {
    // Leave 25 px at each edge to ignore intended FM transition ringing.
    for (size_t x = 25; x + 25 < capture.width; ++x) {
      const auto expected = static_cast<int>(source_pixel(row,x,capture.width));
      result.mae += std::abs(int(capture.rows[row][x])-expected);
      ++result.count;
    }
  }
  result.mae /= result.count ? result.count : 1;
  return result;
}
void save_pgm(const Capture& capture, const char* path) {
  std::ofstream f(path, std::ios::binary);
  if (!f) { std::perror(path); std::exit(2); }
  f << "P5\n" << capture.width << ' ' << capture.rows.size() << "\n255\n";
  for (const auto& r: capture.rows) f.write(reinterpret_cast<const char*>(r.data()), r.size());
}
void run_case(int ioc, int lpm, int rate, float snr_db, int chunk,
              double max_mae, bool image_out = false) {
  Format f; f.ioc=ioc; f.lpm=lpm; f.max_rows=9;
  assert(orcsdr::wefax::valid_format(f));
  Decoder decoder(f);
  Capture capture;
  decoder.set_row_sink(Capture::row,&capture);
  decoder.start_manual();
  auto pcm=encode(rate,lpm,orcsdr::wefax::pixel_width(ioc),9,22000,snr_db);
  send(decoder,pcm,rate==48000,chunk);
  assert(capture.rows.size()==9);
  assert(decoder.metrics().rows==9);
  assert(decoder.metrics().capped);
  assert(!decoder.metrics().receiving);
  assert(capture.width==orcsdr::wefax::pixel_width(ioc));
  const auto score=compare(capture);
  std::printf("PASS IOC=%d LPM=%d PCM=%d SNR=%5.1f chunk=%d width=%d rows=%zu MAE=%.3f (limit %.3f)\n",
              ioc,lpm,rate,snr_db,chunk,capture.width,capture.rows.size(),score.mae,max_mae);
  assert(score.mae <= max_mae);
  if (image_out) {
    const char* path = std::getenv("WEFAX_TEST_PGM");
    if (path && *path) save_pgm(capture,path);
  }
}
void check_invariance() {
  Format f;f.max_rows=9;
  auto pcm=encode(48000,120,1810,9);
  Capture a,b;
  Decoder da(f), db(f);
  da.set_row_sink(Capture::row,&a);
  db.set_row_sink(Capture::row,&b);
  da.start_manual();db.start_manual();
  send(da,pcm,true,1);
  send(db,pcm,true,1789);
  assert(a.rows==b.rows);
  std::puts("PASS arbitrary 48k chunk boundaries are bit-identical");
}
void check_silence_and_configuration() {
  Decoder d;
  Format invalid;invalid.lpm=77;
  assert(!d.configure(invalid));
  assert(d.format().lpm==120);
  Format valid;valid.ioc=288;valid.lpm=90;valid.max_rows=3;
  assert(d.configure(valid));
  Capture c;d.set_row_sink(Capture::row,&c);
  d.start_manual();
  assert(!d.configure(valid));
  std::vector<int16_t> silence(12000*60/90*3,0);
  d.process_pcm_12k(silence.data(),silence.size());
  assert(c.rows.size()==3);
  assert(d.metrics().silent_pcm>0);
  assert(!d.metrics().signal_present);
  assert(d.metrics().capped);
  d.reset();
  assert(d.metrics().rows==0);
  assert(!d.metrics().receiving);
  std::puts("PASS format rejection, bounded silence, reset");
}
void check_partial() {
  Decoder d;
  Capture c;
  d.set_row_sink(Capture::row,&c);
  d.start_manual();
  auto pcm=encode(12000,120,1810,2);
  d.process_pcm_12k(pcm.data(),pcm.size()/2);
  assert(c.rows.size()==1);
  d.stop_manual();
  assert(c.rows.size()==1);
  assert(!d.metrics().receiving);
  std::puts("PASS manual abort never invents a partial row");
}
} // namespace
int main() {
  check_silence_and_configuration();
  check_partial();
  run_case(576,120,12000,80.0,311,13.0,true);
  run_case(576,120,12000,18.0,313,28.0);
  run_case(576,120,48000,80.0,997,13.0);
  run_case(288,60,12000,80.0,487,13.0);
  run_case(576,240,12000,80.0,251,20.0);
  run_case(288,90,48000,22.0,727,25.0);
  check_invariance();
  std::puts("WEFAX host decoder tests all passed.");
}
