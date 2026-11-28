// Verifies the Pluto transmit path from the CardPuter Zero.
//
// Defaults to maximum attenuation so the test proves the data path without
// radiating anything meaningful. Pass an explicit attenuation to change that.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pluto_transmitter.hpp"

int main(int argc, char** argv) {
  const std::uint32_t freq = argc > 1 ? std::uint32_t(std::strtoul(argv[1], nullptr, 0)) : 435000000u;
  const std::uint32_t rate = argc > 2 ? std::uint32_t(std::strtoul(argv[2], nullptr, 0)) : 2400000u;
  const int seconds = argc > 3 ? std::atoi(argv[3]) : 3;
  const double atten = argc > 4 ? std::atof(argv[4]) : orc::linux_sdr::PlutoTransmitter::kSafeAttenuationDb;

  orc::linux_sdr::PlutoTransmitter tx;
  try {
    tx.open();
    tx.configure(freq, rate);
  } catch (const std::exception& error) {
    std::printf("open/configure failed: %s\n", error.what());
    return 2;
  }
  std::printf("TX LO %u Hz  rate %u S/s  attenuation %.2f dB  armed=%s\n", tx.frequency_hz(),
              tx.sample_rate_sps(), tx.attenuation_db(), tx.armed() ? "yes" : "no");

  if (atten < orc::linux_sdr::PlutoTransmitter::kSafeAttenuationDb ||
      atten > 0.0)
    std::printf("(requested %.2f dB was clamped into range)\n", atten);

  tx.set_attenuation_db(atten);
  std::printf("after set: %.2f dB  armed=%s\n", tx.attenuation_db(), tx.armed() ? "yes" : "no");

  // A tone at +100 kHz so a receiver tuned to the LO sees a carrier, not DC.
  const std::size_t block = 2 * 24000;  // 24000 complex samples
  std::vector<std::uint8_t> iq(block);
  const double offset = 100000.0;
  for (std::size_t n = 0; n < block / 2; ++n) {
    const double phase = 2.0 * M_PI * offset * double(n) / double(tx.sample_rate_sps());
    // 0.5 of full scale: -6 dB, so the DAC has headroom.
    iq[n * 2 + 0] = std::uint8_t(128 + int(63.0 * std::cos(phase)));
    iq[n * 2 + 1] = std::uint8_t(128 + int(63.0 * std::sin(phase)));
  }

  std::uint64_t pairs_written = 0;
  const auto start = std::chrono::steady_clock::now();
  while (true) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    if (elapsed >= seconds * 1000) break;
    std::size_t offset_bytes = 0;
    while (offset_bytes < iq.size()) {
      const std::size_t wrote = tx.write_cu8(iq.data() + offset_bytes, iq.size() - offset_bytes);
      if (!wrote) break;
      offset_bytes += wrote;
      pairs_written += wrote / 2;
    }
    if (offset_bytes < iq.size()) {
      std::printf("write stalled after %zu bytes\n", offset_bytes);
      break;
    }
  }
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  const double seconds_actual = double(elapsed_ms) / 1000.0;
  std::printf("wrote %llu complex samples in %.2f s = %.2f MS/s (device %u S/s)\n",
              (unsigned long long)pairs_written, seconds_actual,
              double(pairs_written) / seconds_actual / 1e6, tx.sample_rate_sps());
  std::printf("total %llu samples\n", (unsigned long long)tx.samples_written());

  // Leave it muted, whatever was asked before.
  tx.set_attenuation_db(orc::linux_sdr::PlutoTransmitter::kSafeAttenuationDb);
  std::printf("restored to %.2f dB\n", tx.attenuation_db());
  tx.close();
  return pairs_written ? 0 : 1;
}
