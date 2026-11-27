// Measures whether this RTL front end can actually hear a band.
//
// Written because the numbers decide whether porting the DMR pipeline is worth
// anything: a decoder on a deaf front end produces a feature that can never
// receive. Reports RMS and peak for CU8 IQ at each frequency, against a band
// known to work (FM broadcast) as the control.
//
// Build on the device:
//   g++ -std=c++17 -O2 -Isrc band_probe.cpp src/rtl_receiver.cpp -lrtlsdr -o band_probe
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "rtl_receiver.hpp"

int main() {
  struct Point { const char* label; unsigned hz; };
  const Point points[] = {
      {"7.074 MHz  (FT8 40m, works)", 7074000},
      {"96.1 MHz   (FM broadcast, works)", 96100000},
      {"152.1575 MHz (DMR/VHF target)", 152157500},
      {"155.0 MHz", 155000000},
      {"460.0 MHz  (DMR UHF target)", 460000000},
      {"463.4 MHz  (DMR, decoded on Tab5)", 463400000},
      {"433.92 MHz  (ISM)", 433920000},
      {"857.0 MHz  (control: above the tuner)", 857000000},
      {"906.875 MHz (Meshtastic LoRa)", 906875000},
      {"915.0 MHz  (US ISM)", 915000000},
  };

  orc::linux_sdr::RtlReceiver receiver;
  receiver.open(0);
  receiver.set_tuner_gain_auto();

  std::printf("%-36s %8s %8s %8s\n", "point", "rms", "peak", "crest");
  for (const auto& point : points) {
    receiver.configure(point.hz, 2400000);
    // Discard the first block: retuning leaves a transient in the buffer.
    std::vector<std::uint8_t> buffer(256 * 1024);
    receiver.read_cu8(buffer.data(), buffer.size());
    const size_t got = receiver.read_cu8(buffer.data(), buffer.size());
    if (!got) {
      std::printf("%-36s  read failed\n", point.label);
      continue;
    }
    double sum = 0;
    int peak = 0;
    for (size_t n = 0; n < got; ++n) {
      const int value = int(buffer[n]) - 128;
      sum += double(value) * value;
      peak = std::max(peak, std::abs(value));
    }
    const double rms = std::sqrt(sum / double(got));
    std::printf("%-36s %8.2f %8d %8.2f\n", point.label, rms, peak, peak / std::max(0.01, rms));
  }
  receiver.close();
  return 0;
}
