#include "orcsdr_dsp/demod.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace orcsdr::dsp;
#define CHECK(expr) do { if(!(expr)) { std::fprintf(stderr,"CHECK failed: %s:%d: %s\n",__FILE__,__LINE__,#expr); std::abort(); } } while(0)

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 960000.0;

static uint8_t q8(double v) {
  const long x = std::lround(v * 127.5 + 127.5);
  return static_cast<uint8_t>(x < 0 ? 0 : (x > 255 ? 255 : x));
}

// IQ for a carrier at `offset_hz` whose complex envelope is given per sample by fn(t) (returns {i,q}).
template <typename F>
static std::vector<uint8_t> make_iq(double seconds, double noise_sigma, F fn) {
  const size_t n = static_cast<size_t>(seconds * kFs);
  std::vector<uint8_t> iq(2 * n);
  std::mt19937 rng(7);
  std::normal_distribution<double> noise(0.0, noise_sigma);
  for (size_t k = 0; k < n; ++k) {
    double i, q;
    fn(k / kFs, &i, &q);
    iq[2 * k] = q8(i + noise(rng));
    iq[2 * k + 1] = q8(q + noise(rng));
  }
  return iq;
}

struct Tone { double amplitude, snr_db; };
// Amplitude of the `freq` component and signal-to-everything-else ratio of the last half second.
static Tone analyse(const std::vector<float>& audio, double freq) {
  const size_t skip = 24000;  // 0.5 s settling
  CHECK(audio.size() > skip + 12000);
  double re = 0, im = 0, power = 0, mean = 0;
  const size_t n = audio.size() - skip;
  for (size_t k = 0; k < n; ++k) mean += audio[skip + k];
  mean /= n;
  for (size_t k = 0; k < n; ++k) {
    const double x = audio[skip + k] - mean, ph = 2 * kPi * freq * k / 48000.0;
    re += x * std::cos(ph);
    im += x * std::sin(ph);
    power += x * x;
  }
  const double amp = 2 * std::sqrt(re * re + im * im) / n;
  const double tone_power = amp * amp / 2, total = power / n;
  const double rest = std::max(total - tone_power, 1e-12);
  return {amp, 10 * std::log10(tone_power / rest)};
}

static std::vector<float> run(Demod& d, const std::vector<uint8_t>& iq) {
  std::vector<float> out(iq.size() / 40 + 64);
  size_t total = 0;
  // Feed in 16 KiB blocks like the receiver does.
  for (size_t off = 0; off < iq.size(); off += 16384) {
    const size_t len = std::min<size_t>(16384, iq.size() - off);
    total += d.process(iq.data() + off, len, out.data() + total, out.size() - total);
  }
  out.resize(total);
  return out;
}

int main() {
  // atan2 approximation accuracy.
  double worst = 0;
  for (int a = -180; a <= 180; ++a)
    for (double r : {0.001, 0.3, 1.0, 50.0}) {
      const double y = r * std::sin(a * kPi / 180), x = r * std::cos(a * kPi / 180);
      worst = std::max(worst, std::fabs(static_cast<double>(fast_atan2(static_cast<float>(y), static_cast<float>(x))) -
                                        std::atan2(y, x)));
    }
  std::printf("atan2 worst error %.2e rad\n", worst);
  CHECK(worst < 2e-4);

  // Output rate: 960 kS/s in, 48 kS/s out.
  {
    Demod d;
    std::vector<uint8_t> iq(2 * 96000, 128);
    std::vector<float> out(5000);
    const size_t n = d.process(iq.data(), iq.size(), out.data(), out.size());
    CHECK(n >= 4799 && n <= 4801);
  }

  // Wideband FM: 1 kHz tone, 50 kHz deviation.
  {
    Demod d;
    d.set_mode(Mode::wfm);
    const double beta = 50000.0 / 1000.0;
    auto iq = make_iq(1.5, 0.01, [&](double t, double* i, double* q) {
      const double ph = -beta * std::cos(2 * kPi * 1000 * t);
      *i = 0.7 * std::cos(ph);
      *q = 0.7 * std::sin(ph);
    });
    const Tone t = analyse(run(d, iq), 1000.0);
    std::printf("WFM 1 kHz / 50 kHz dev: amplitude %.3f (expect ~0.60 after de-emphasis), SNR %.1f dB\n", t.amplitude, t.snr_db);
    CHECK(t.amplitude > 0.54 && t.amplitude < 0.66);
    CHECK(t.snr_db > 25.0);
  }

  // Wideband FM with a strong carrier 300 kHz away: the channel filter must reject it.
  {
    Demod d;
    d.set_mode(Mode::wfm);
    const double beta = 50000.0 / 1000.0;
    auto iq = make_iq(1.5, 0.01, [&](double t, double* i, double* q) {
      const double ph = -beta * std::cos(2 * kPi * 1000 * t);
      const double ph2 = 2 * kPi * 300000.0 * t;
      *i = 0.4 * std::cos(ph) + 0.35 * std::cos(ph2);
      *q = 0.4 * std::sin(ph) + 0.35 * std::sin(ph2);
    });
    const Tone t = analyse(run(d, iq), 1000.0);
    std::printf("WFM with +300 kHz interferer: amplitude %.3f, SNR %.1f dB\n", t.amplitude, t.snr_db);
    CHECK(t.amplitude > 0.5 && t.snr_db > 18.0);
  }

  // Narrow FM: 1 kHz tone, 3 kHz deviation (+/-5 kHz = full scale).
  {
    Demod d;
    d.set_mode(Mode::nfm);
    const double beta = 3000.0 / 1000.0;
    auto iq = make_iq(1.5, 0.01, [&](double t, double* i, double* q) {
      const double ph = -beta * std::cos(2 * kPi * 1000 * t);
      *i = 0.7 * std::cos(ph);
      *q = 0.7 * std::sin(ph);
    });
    const Tone t = analyse(run(d, iq), 1000.0);
    std::printf("NFM 1 kHz / 3 kHz dev: amplitude %.3f (expect ~0.60), SNR %.1f dB\n", t.amplitude, t.snr_db);
    CHECK(t.amplitude > 0.5 && t.amplitude < 0.7);
    CHECK(t.snr_db > 20.0);
  }

  // AM: 1 kHz tone, 50% modulation.
  {
    Demod d;
    d.set_mode(Mode::am);
    auto iq = make_iq(2.5, 0.005, [&](double t, double* i, double* q) {
      *i = 0.5 * (1.0 + 0.5 * std::sin(2 * kPi * 1000 * t));
      *q = 0.0;
    });
    std::vector<float> audio = run(d, iq);
    const Tone t = analyse(audio, 1000.0);
    std::printf("AM 1 kHz / 50%%: amplitude %.3f (expect ~0.25), SNR %.1f dB\n", t.amplitude, t.snr_db);
    CHECK(t.amplitude > 0.18 && t.amplitude < 0.34);
    CHECK(t.snr_db > 20.0);
    for (float v : audio) CHECK(v >= -1.0f && v <= 1.0f);
  }

  // 2.4 MS/s input: same audio out, with a strong neighbour 600 kHz away rejected.
  {
    constexpr double kFs2 = 2400000.0;
    Demod d;
    d.set_input_rate(2400000);
    d.set_mode(Mode::wfm);
    const size_t n = static_cast<size_t>(1.5 * kFs2);
    std::vector<uint8_t> iq(2 * n);
    std::mt19937 rng(11);
    std::normal_distribution<double> noise(0.0, 0.01);
    for (size_t k = 0; k < n; ++k) {
      const double t = k / kFs2;
      const double ph = -50.0 * std::cos(2 * kPi * 1000 * t), ph2 = 2 * kPi * 600000.0 * t;
      iq[2 * k] = q8(0.4 * std::cos(ph) + 0.35 * std::cos(ph2) + noise(rng));
      iq[2 * k + 1] = q8(0.4 * std::sin(ph) + 0.35 * std::sin(ph2) + noise(rng));
    }
    std::vector<float> out(n / 40 + 64);
    size_t total = 0;
    for (size_t off = 0; off < iq.size(); off += 16384) {
      const size_t len = std::min<size_t>(16384, iq.size() - off);
      total += d.process(iq.data() + off, len, out.data() + total, out.size() - total);
    }
    out.resize(total);
    const Tone t = analyse(out, 1000.0);
    std::printf("WFM at 2.4 MS/s with +600 kHz interferer: amplitude %.3f, SNR %.1f dB, %zu samples\n", t.amplitude, t.snr_db, total);
    CHECK(t.amplitude > 0.5 && t.snr_db > 18.0);
    CHECK(total > 71000 && total < 73000);  // 1.5 s at 48 kHz
  }

  // 2.4 MS/s with a strong station 400 kHz away: the worst alias of the 480 kS/s decimator (it folds to 80 kHz).
  {
    constexpr double kFs2 = 2400000.0;
    Demod d;
    d.set_input_rate(2400000);
    d.set_mode(Mode::wfm);
    const size_t n = static_cast<size_t>(1.5 * kFs2);
    std::vector<uint8_t> iq(2 * n);
    std::mt19937 rng(13);
    std::normal_distribution<double> noise(0.0, 0.01);
    for (size_t k = 0; k < n; ++k) {
      const double t = k / kFs2;
      const double ph = -50.0 * std::cos(2 * kPi * 1000 * t), ph2 = 2 * kPi * 400000.0 * t + 8.0 * std::sin(2 * kPi * 1700 * t);
      iq[2 * k] = q8(0.3 * std::cos(ph) + 0.35 * std::cos(ph2) + noise(rng));
      iq[2 * k + 1] = q8(0.3 * std::sin(ph) + 0.35 * std::sin(ph2) + noise(rng));
    }
    std::vector<float> out(n / 40 + 64);
    size_t total = 0;
    for (size_t off = 0; off < iq.size(); off += 16384) {
      const size_t len = std::min<size_t>(16384, iq.size() - off);
      total += d.process(iq.data() + off, len, out.data() + total, out.size() - total);
    }
    out.resize(total);
    const Tone t = analyse(out, 1000.0);
    std::printf("WFM at 2.4 MS/s with a modulated station 400 kHz away: wanted tone %.3f, SNR %.1f dB\n", t.amplitude, t.snr_db);
    CHECK(t.amplitude > 0.4 && t.snr_db > 10.0);
  }

  // SSB: a suppressed-carrier tone 1.5 kHz above the tuned frequency is audible as 1.5 kHz in USB and
  // rejected in LSB (and the reverse for a tone below).
  {
    auto ssb_run = [&](Mode mode, double offset) {
      Demod d;
      d.set_mode(mode);
      auto iq = make_iq(2.0, 0.003, [&](double t, double* i, double* q) {
        *i = 0.5 * std::cos(2 * kPi * offset * t);
        *q = 0.5 * std::sin(2 * kPi * offset * t);
      });
      return run(d, iq);
    };
    auto rms = [](const std::vector<float>& v) {
      double a = 0;
      const size_t from = v.size() / 2;
      for (size_t k = from; k < v.size(); ++k) a += v[k] * v[k];
      return std::sqrt(a / (v.size() - from));
    };
    const double usb_on = rms(ssb_run(Mode::usb, 1500)), usb_off = rms(ssb_run(Mode::usb, -1500));
    const double lsb_on = rms(ssb_run(Mode::lsb, -1500)), lsb_off = rms(ssb_run(Mode::lsb, 1500));
    std::printf("SSB: USB wanted %.3f unwanted %.4f; LSB wanted %.3f unwanted %.4f\n", usb_on, usb_off, lsb_on, lsb_off);
    CHECK(usb_on > 0.1 && lsb_on > 0.1);
    CHECK(usb_off < usb_on / 30 && lsb_off < lsb_on / 30);
  }

  std::puts("PASS all");
}
