// Host tests call the exact source compiled by ESP32 OrcSDR, not a decoder fork.
#include "acars_core.hpp"
#include "acars_selftest.hpp"
#include "adsb_decoder.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace orcsdr;
struct Chain {
  acars::Frontend240k frontend;
  acars::Decoder decoder;
  unsigned messages = 0;
  bool valid = true;
};
void on_message(void* ctx, const acars::Message& m) {
  auto& c = *static_cast<Chain*>(ctx);
  ++c.messages;
  c.valid &= m.frequency_hz == 131550000 &&
             std::strcmp(m.tail, acars_selftest::Signal::tail()) == 0 &&
             m.label[0] == 'Q' && m.label[1] == '0' &&
             m.has_text && std::strcmp(m.text, acars_selftest::Signal::text()) == 0 &&
             !m.corrected && m.corrected_bits == 0 && m.end_marker == 0x03;
}
void on_iq(void* ctx, float i, float q) {
  static_cast<Chain*>(ctx)->decoder.process_channel_iq(i, q);
}
bool chunked_acars(size_t requested_bytes) {
  Chain c;
  c.decoder.set_callback(on_message, &c);
  c.decoder.set_frequency(131550000);
  std::array<uint8_t,8192> samples{};
  for (int pass=0; pass<2; ++pass) {
    if (pass != 0) {
      // A retune or sample gap requires front-end AND decoder reset.
      c.frontend.reset();
      c.decoder.reset_stream(); // preserves cumulative CRC/message statistics
    }
    acars_selftest::Signal signal(3023, 0.0, 0x1234);
    for (;;) {
      const size_t got=signal.next(samples.data(), requested_bytes);
      if (!got) break;
      c.frontend.process_cu8(samples.data(), got/2, on_iq, &c);
    }
    if (c.messages != static_cast<unsigned>(pass+1) || !c.valid ||
        c.decoder.stats().crc_ok != static_cast<unsigned>(pass+1))
      return false;
  }
  return true;
}
bool acars_crc_regression() {
  std::vector<uint8_t> frame;
  const uint8_t content[] = {
    '2','N','1','2','3','4','5','6',0x15,'Q','0','1',0x02,'A',0x03
  };
  for (auto b:content) frame.push_back(acars::odd_parity_byte(b));
  const auto crc=acars::crc16_kermit(frame.data(),frame.size());
  frame.push_back(static_cast<uint8_t>(crc));
  frame.push_back(static_cast<uint8_t>(crc>>8));
  if (acars::crc16_kermit(frame.data(),frame.size()) != 0) return false;
  auto check=[&](std::vector<uint8_t> data,bool want_repair,unsigned bits) {
    bool repaired=false;uint8_t fixed=0;
    return acars::validate_or_repair(data.data(),data.size(),repaired,fixed) &&
           repaired==want_repair && fixed==bits && data==frame;
  };
  if (!check(frame,false,0)) return false;
  auto single=frame;single[3]^=0x04;
  if (!check(single,true,1)) return false;
  auto double_error=frame;double_error[2]^=0x02;double_error[9]^=0x08;
  if (!check(double_error,true,2)) return false;
  auto triple=frame;triple[2]^=0x02;triple[5]^=0x01;triple[9]^=0x08;
  bool repaired=false;uint8_t count=0;
  return !acars::validate_or_repair(triple.data(),triple.size(),repaired,count);
}
}

int main() {
  const auto result=orcsdr::acars_selftest::run(nullptr);
  std::cout<<"ACARS fixture cases="<<result.cases<<" passed="<<result.passed
           <<" missed="<<result.missed<<" duplicates="<<result.duplicates<<"\n";
  if (!result.allocated || result.cases!=10 || result.passed!=10 ||
      result.missed || result.duplicates) return 1;
  if (!acars_crc_regression()) { std::cerr<<"ACARS CRC validation/repair failed\n"; return 2; }
  for (const size_t size : {size_t(74),size_t(510),size_t(4096),size_t(8192)})
    if (!chunked_acars(size)) { std::cerr<<"ACARS chunk/reset failed: "<<size<<"\n";return 3; }
  if (!orcsdr::adsb_rx::Decoder::self_check()) {
    std::cerr<<"ADS-B known-frame/CRC/IQ regression failed\n";return 4;
  }
  std::cout<<"ACARS CRC, 1/2-bit verified repairs, reset, chunking: PASS\n"
           <<"ADS-B deterministic known frames and CU8 self-check: PASS\n";
  return 0;
}
