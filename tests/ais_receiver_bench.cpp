// Host-only microbenchmark, not proof of ESP32-P4 sustained realtime performance.
#include "ais_receiver.hpp"
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
int main() {
  using clock=std::chrono::steady_clock;
  constexpr uint32_t seconds=4;
  std::vector<uint8_t> iq(2*orcsdr::ais_rx::kInputRateHz*seconds,127);
  static orcsdr::ais_rx::Receiver rx;
  const auto begin=clock::now();
  for(size_t off=0;off<iq.size();off+=4096) {
    const size_t size=std::min<size_t>(4096,iq.size()-off);
    if(!rx.process_cu8(iq.data()+off,size,orcsdr::ais_rx::kInputRateHz,nullptr,nullptr))return 1;
  }
  const double elapsed=std::chrono::duration<double>(clock::now()-begin).count();
  std::printf("AIS receiver host bench: %u seconds IQ, %.3f s processing, %.2fx realtime, %llu samples\n",
    seconds,elapsed,seconds/elapsed,static_cast<unsigned long long>(rx.stats().iq_samples));
  return rx.stats().iq_samples==static_cast<uint64_t>(seconds)*orcsdr::ais_rx::kInputRateHz?0:1;
}
