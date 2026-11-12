// AIS receiver stress measurements with an independent signal model: carrier offset, noise, sub-symbol timing,
// symbol-clock error, low level and noise-only false frames.
//
// The burst generator here is separate from the one in ais_receiver_tests.cpp: the Gaussian-filtered NRZ frequency
// pulse is evaluated analytically (difference of error functions) instead of by a sampled FIR, payloads are random,
// every trial starts at a different sub-symbol timing, and receiver noise precedes and follows each burst.
//
//   ais_receiver_stress            prints the tables and enforces the regression floors below
//   ais_receiver_stress 600        also runs 600 s of noise-only input (default 60 s)
//
// A synthetic channel says nothing about a real antenna, front end or multipath; it bounds what the DSP can do.
#include "ais_receiver.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace rx = orcsdr::ais_rx;
namespace {
const double kPi = 3.14159265358979323846;
std::mt19937 g_rng(12345);
int g_matched = 0, g_unexpected = 0, g_false_packets = 0;
std::vector<uint8_t> g_want;

void on_expected(const rx::Packet& p, void*) {
  if (p.bit_count == g_want.size() * 8 && !std::memcmp(p.bytes, g_want.data(), g_want.size())) ++g_matched;
  else ++g_unexpected;
}
void on_false(const rx::Packet&, void*) { ++g_false_packets; }

uint16_t fcs(const std::vector<uint8_t>& data) {
  uint16_t crc = 0xffff;
  for (uint8_t byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0x8408 : 0));
  }
  return static_cast<uint16_t>(~crc);
}

std::vector<int> wire_bits(const std::vector<uint8_t>& payload) {
  std::vector<int> bits;
  for (int i = 0; i < 24; ++i) bits.push_back(i % 2);
  const int flag[8] = {0, 1, 1, 1, 1, 1, 1, 0};
  bits.insert(bits.end(), flag, flag + 8);
  std::vector<uint8_t> bytes = payload;
  const uint16_t crc = fcs(payload);
  bytes.push_back(static_cast<uint8_t>(crc & 255));
  bytes.push_back(static_cast<uint8_t>(crc >> 8));
  int ones = 0;
  for (uint8_t byte : bytes)
    for (int n = 0; n < 8; ++n) {
      const int bit = (byte >> n) & 1;
      bits.push_back(bit);
      ones = bit ? ones + 1 : 0;
      if (ones == 5) { bits.push_back(0); ones = 0; }
    }
  bits.insert(bits.end(), flag, flag + 8);
  return bits;
}

// CU8 IQ of one burst on channel 0 (AIS 1) or 1 (AIS 2), with noise before and after it.
std::vector<uint8_t> burst(const std::vector<uint8_t>& payload, int channel, double offset_hz, double amplitude, double sigma,
                           int shift_samples, double clock_ppm) {
  const std::vector<int> bits = wire_bits(payload);
  std::vector<int> levels;
  int level = 1;
  for (int bit : bits) { if (!bit) level ^= 1; levels.push_back(level); }
  const double sps = 25.0 * (1 + clock_ppm * 1e-6);
  const int lead = 4800 + shift_samples, tail = 4800;
  const size_t count = static_cast<size_t>(levels.size() * sps) + 200;
  const double sigma_t = std::sqrt(std::log(2.0)) / (2 * kPi * 0.4);   // BT 0.4, in bit periods
  std::vector<double> shaped(count, 0.0);
  for (size_t k = 0; k < count; ++k) {
    const double t = (static_cast<double>(k) - 100) / sps;
    const int nearest = static_cast<int>(std::floor(t));
    double sum = 0;
    for (int j = nearest - 4; j <= nearest + 4; ++j) {
      if (j < 0 || j >= static_cast<int>(levels.size())) continue;
      const double a = (t - j) / (sigma_t * std::sqrt(2.0)), b = (t - j - 1) / (sigma_t * std::sqrt(2.0));
      sum += (levels[j] ? 1.0 : -1.0) * 0.5 * (std::erf(a) - std::erf(b));
    }
    shaped[k] = sum;
  }
  std::normal_distribution<double> noise(0, sigma);
  std::vector<uint8_t> out;
  out.reserve((lead + count + tail) * 2);
  auto push = [&](double i, double q) {
    auto quantise = [](double v) {
      const long r = std::lround(127.5 + 128.0 * v);
      return static_cast<uint8_t>(r < 0 ? 0 : r > 255 ? 255 : r);
    };
    out.push_back(quantise(i + noise(g_rng)));
    out.push_back(quantise(q + noise(g_rng)));
  };
  double phase = 0;
  const double carrier = (channel ? 25000.0 : -25000.0) + offset_hz;
  for (int k = 0; k < lead; ++k) push(0, 0);
  for (size_t k = 0; k < count; ++k) {
    phase += 2 * kPi * (carrier + 2400.0 * shaped[k]) / 240000.0;
    push(amplitude * std::cos(phase), amplitude * std::sin(phase));
  }
  for (int k = 0; k < tail; ++k) push(0, 0);
  return out;
}

std::vector<uint8_t> random_payload(size_t bytes) {
  std::vector<uint8_t> p(bytes);
  for (auto& b : p) b = static_cast<uint8_t>(g_rng() & 255);
  p[0] = static_cast<uint8_t>((p[0] & 3) | 4);
  return p;
}

std::unique_ptr<rx::Receiver> g_receiver = std::make_unique<rx::Receiver>();

// Percentage of bursts decoded exactly once, byte for byte.
double decoded_pct(size_t bytes, double offset_hz, double amplitude, double sigma, double clock_ppm, int trials) {
  int ok = 0;
  for (int t = 0; t < trials; ++t) {
    g_want = random_payload(bytes);
    g_matched = 0;
    g_receiver->reset();
    const auto iq = burst(g_want, t & 1, offset_hz, amplitude, sigma, t % 25, clock_ppm);
    g_receiver->process_cu8(iq.data(), iq.size(), 240000, on_expected, nullptr);
    if (g_matched == 1) ++ok;
  }
  return 100.0 * ok / trials;
}

int g_failures = 0;
void require(bool condition, const char* what) {
  if (!condition) { std::printf("FLOOR NOT MET: %s\n", what); ++g_failures; }
}
}  // namespace

int main(int argc, char** argv) {
  {  // fast_atan2 accuracy against libm over the whole circle, several magnitudes
    double worst = 0;
    for (int i = 0; i < 400000; ++i) {
      const double angle = (i / 400000.0) * 2 * kPi - kPi, scale = std::pow(10.0, (i % 7) - 3);
      const float y = static_cast<float>(std::sin(angle) * scale), x = static_cast<float>(std::cos(angle) * scale);
      const double got = static_cast<double>(rx::fast_atan2(y, x));
      if (!std::isfinite(got)) { std::puts("FLOOR NOT MET: fast_atan2 returned a non-finite value"); ++g_failures; break; }
      double diff = std::fmod(got - std::atan2(static_cast<double>(y), static_cast<double>(x)), 2 * kPi);   // circular error:
      if (diff > kPi) diff -= 2 * kPi;                                                                      // +pi and -pi are the same angle
      if (diff < -kPi) diff += 2 * kPi;
      worst = std::max(worst, std::fabs(diff));
    }
    std::printf("fast_atan2 worst error %.2e rad\n", worst);
    if (worst > 3e-4) { std::puts("FLOOR NOT MET: fast_atan2 error above 3e-4 rad"); ++g_failures; }
  }
  const int trials = 50;
  std::puts("-- carrier offset, strong signal (amplitude 0.5, noise sigma 0.01), 168-bit payloads, all 25 timing shifts");
  for (double offset : {0., 500., -500., 1000., -1000., 1500., -1500., 2000., -2000., 2500., 3000., -3000., 4000., -4000.}) {
    const double pct = decoded_pct(21, offset, 0.5, 0.01, 0, trials);
    std::printf("offset %+6.0f Hz: %5.1f%%\n", offset, pct);
    if (std::fabs(offset) <= 3000.) require(pct >= 98., "strong 168-bit bursts decode at up to +/-3 kHz offset");
  }
  std::puts("-- carrier offset, 424-bit payloads (two-slot static and voyage size)");
  for (double offset : {0., 1000., 2000., 3000., -3000.}) {
    const double pct = decoded_pct(53, offset, 0.5, 0.01, 0, trials);
    std::printf("offset %+6.0f Hz: %5.1f%%\n", offset, pct);
    require(pct >= 98., "strong 424-bit bursts decode at up to +/-3 kHz offset");
  }
  std::puts("-- symbol clock error, 1008-bit payloads");
  for (double ppm : {0., 50., -50., 200., 500.}) {
    const double pct = decoded_pct(126, 0, 0.5, 0.01, ppm, 30);
    std::printf("clock %+5.0f ppm: %5.1f%%\n", ppm, pct);
    if (std::fabs(ppm) <= 200.) require(pct >= 95., "five-slot bursts decode at up to +/-200 ppm clock error");
  }
  std::puts("-- sensitivity, 168-bit payloads (SNR in a 25 kHz bandwidth)");
  const double amplitude = 0.1;
  for (double sigma : {0.02, 0.04, 0.06, 0.08, 0.10, 0.14}) {
    const double snr = 10 * std::log10(amplitude * amplitude / (2 * sigma * sigma)) + 10 * std::log10(240.0 / 25.0);
    const double at0 = decoded_pct(21, 0, amplitude, sigma, 0, trials), at500 = decoded_pct(21, 500, amplitude, sigma, 0, trials),
                 at1000 = decoded_pct(21, 1000, amplitude, sigma, 0, trials), at2500 = decoded_pct(21, 2500, amplitude, sigma, 0, trials);
    std::printf("SNR %5.1f dB: 0 Hz %5.1f%%   500 Hz %5.1f%%   1000 Hz %5.1f%%   2500 Hz %5.1f%%\n", snr, at0, at500, at1000, at2500);
    if (snr > 20.) require(at0 >= 98. && at500 >= 98. && at1000 >= 98. && at2500 >= 95., "20 dB bursts decode at any offset to 2.5 kHz");
    if (snr > 14. && snr < 16.) require(at0 >= 95. && at1000 >= 85., "15 dB bursts decode, including at 1 kHz offset");
  }
  std::puts("-- low level, no added noise (quantisation only)");
  for (double a : {0.02, 0.01, 0.005}) std::printf("amplitude %.3f: %5.1f%%\n", a, decoded_pct(21, 0, a, 0.0, 0, trials));
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 60;
  std::printf("-- noise only, %d s (Gaussian, sigma 6.4 LSB)\n", seconds);
  g_receiver->reset();
  std::normal_distribution<double> noise(127.5, 6.4);
  std::vector<uint8_t> block(48000);
  for (int s = 0; s < seconds * 10; ++s) {
    for (auto& b : block) { const double v = noise(g_rng); b = static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }
    g_receiver->process_cu8(block.data(), block.size(), 240000, on_false, nullptr);
  }
  const rx::Stats stats = g_receiver->stats();
  for (int c = 0; c < 2; ++c)
    std::printf("AIS %d: candidates %llu, crc failed %llu, bad stuffing %llu, invalid length %llu\n", c + 1,
                static_cast<unsigned long long>(stats.channels[c].candidate_frames),
                static_cast<unsigned long long>(stats.channels[c].crc_failed),
                static_cast<unsigned long long>(stats.channels[c].bad_stuffing),
                static_cast<unsigned long long>(stats.channels[c].invalid_length));
  std::printf("false packets: %d (a 16-bit FCS passes about one random candidate in 65536)\n", g_false_packets);
  std::printf("unexpected packets during the bursts above: %d\n", g_unexpected);
  require(g_unexpected == 0, "no packet other than the transmitted one is ever reported during a burst");
  std::printf("%s\n", g_failures ? "AIS receiver stress: FLOORS NOT MET" : "AIS receiver stress floors met (synthetic channel only)");
  return g_failures ? 1 : 0;
}
