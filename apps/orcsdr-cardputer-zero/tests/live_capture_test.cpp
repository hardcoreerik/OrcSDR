#include "live_capture.hpp"
#include "acars_selftest.hpp"
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace orc::linux_sdr;
int main() {
  LiveCaptureOptions opts{};
  opts.decoder=LiveDecoder::acars;
  opts.center_hz=131490000;
  opts.sample_rate_sps=240000;
  opts.max_blocks=16;
  opts.complex_samples_per_block=8192;
  orcsdr::acars_selftest::Signal fixture(3023,0.0,0x1234);
  std::ostringstream events;
  auto result=run_live_blocks(opts,[&](std::uint8_t* data,std::size_t cap){
    return fixture.next(data,cap);
  },events,16); // enough preallocated slots for deterministic fast fixture input
  if(result.decoded_messages!=1 || result.known_queue_dropped_samples ||
     result.complex_samples_decoded!=60623 || result.complex_samples_read!=60623 ||
     events.str().find("tail=N123456")==std::string::npos) {
    std::cerr<<"Live IQ pipeline ACARS fixture failed: read="<<result.complex_samples_read
             <<" decoded="<<result.complex_samples_decoded
             <<" drops="<<result.known_queue_dropped_samples
             <<" messages="<<result.decoded_messages<<"\n"<<events.str();
    return 1;
  }
  opts.sample_rate_sps=1024000;
  try {
    (void)run_live_blocks(opts,[](std::uint8_t*,std::size_t){return std::size_t(0);},events);
    return 2;
  } catch(const std::invalid_argument&) {}
  opts.sample_rate_sps=240000;
  try {
    (void)run_live_blocks(opts,[](std::uint8_t*,std::size_t){return std::size_t(3);},events);
    return 3;
  } catch(const std::runtime_error&) {}
  std::cout<<"live producer-consumer, ACARS 60623 samples, CRC-valid message, invalid rate/block: PASS\n";
  return 0;
}