// Verifies the Pluto receive path end to end from the CardPuter Zero: detect,
// open, tune, set a rate, and measure the delivered CU8 rate against nominal.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pluto_receiver.hpp"

int main(int argc, char** argv) {
  const std::uint32_t freq = argc > 1 ? std::uint32_t(std::strtoul(argv[1], nullptr, 0)) : 963000000u;
  const std::uint32_t rate = argc > 2 ? std::uint32_t(std::strtoul(argv[2], nullptr, 0)) : 960000u;
  const int seconds = argc > 3 ? std::atoi(argv[3]) : 5;

  const auto info = orc::linux_sdr::PlutoReceiver::detect();
  if (!info.found) {
    std::printf("no PlutoSDR found\n");
    return 2;
  }
  std::printf("found: uri '%s'  %s\n", info.uri.c_str(), info.name.c_str());

  orc::linux_sdr::PlutoReceiver rx;
  try {
    rx.open(info.uri);
    rx.configure(freq, rate);
  } catch (const std::exception& error) {
    std::printf("open/configure failed: %s\n", error.what());
    return 2;
  }

  std::printf("requested freq %u Hz  rate %u S/s\n", freq, rate);
  std::printf("tuned      %u Hz\n", rx.frequency_hz());
  std::printf("device     %u S/s  decimation %d  delivered %u S/s\n", rx.device_rate_sps(),
              rx.decimation(), rx.sample_rate_sps());
  std::printf("gain       %d tenths dB\n", rx.tuner_gain_tenth_db());

  std::vector<std::uint8_t> buffer(65536);
  std::uint64_t bytes = 0, samples = 0;
  double sum_sq = 0;
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    if (elapsed >= seconds * 1000) break;
    const std::size_t got = rx.read_cu8(buffer.data(), buffer.size());
    if (!got) {
      std::printf("read returned 0 bytes\n");
      break;
    }
    bytes += got;
    for (std::size_t i = 0; i + 1 < got; i += 2) {
      const double i_part = double(int(buffer[i]) - 128);
      const double q_part = double(int(buffer[i + 1]) - 128);
      sum_sq += i_part * i_part + q_part * q_part;
      ++samples;
    }
  }
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  const double seconds_actual = double(elapsed_ms) / 1000.0;
  const double cu8_rate = double(bytes) / seconds_actual;
  const double nominal = double(rx.sample_rate_sps()) * 2.0;  // 2 bytes per I/Q pair
  const double rms = samples ? std::sqrt(sum_sq / (2.0 * double(samples))) : 0.0;

  std::printf("read %.2f MB in %.2f s = %.2f MB/s (nominal %.2f MB/s, %.1f%%)\n",
              double(bytes) / 1e6, seconds_actual, cu8_rate / 1e6, nominal / 1e6,
              nominal > 0 ? 100.0 * cu8_rate / nominal : 0.0);
  std::printf("samples %llu  CU8 rms %.1f\n", (unsigned long long)samples, rms);
  rx.close();
  return bytes ? 0 : 1;
}
