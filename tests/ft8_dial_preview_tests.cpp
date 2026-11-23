#include "../apps/orcsdr-tab5/ui/ft8_dial_preview.hpp"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
using namespace orcsdr::ft8;
int main() {
  Decode d{}; d.utc_epoch=1793303100; d.dial_hz=14074000; d.snr_db=-12;
  d.kind=DecodeKind::cq; std::strcpy(d.callsign,"W1AW");
  std::strcpy(d.grid,"FN31"); std::strcpy(d.message,"CQ W1AW FN31");
  uint8_t brief[64]{}, text[64]{};
  assert(dial_preview::encode(d, 123, 808, brief, text));
  // Golden wire vectors deliberately shared with the opposite repository.
  auto matches_hex = [](const uint8_t* bytes, const char* hex) {
    auto nibble = [](char c) -> unsigned {
      return c >= '0' && c <= '9' ? unsigned(c-'0') :
             c >= 'a' && c <= 'f' ? unsigned(c-'a'+10) : 255u;
    };
    for (size_t i=0; i<64; ++i) {
      const unsigned hi=nibble(hex[i*2]),lo=nibble(hex[i*2+1]);
      if (hi>15 || lo>15 || bytes[i] != uint8_t(hi*16+lo)) return false;
    }
    return hex[128] == 0;
  };
  assert(matches_hex(brief,"4f52444c041102007b00000028030000000000003ca2e36a90c0d60057314157000000000000000000000000464e33310000000000f407000000000037845473"));
  assert(matches_hex(text, "4f52444c041202007b00000028030000000000004351205731415720464e3331000000000000000000000000000000000000000000000000000000007ded8e35"));
  assert(brief[4] == 4 && brief[5] == 17 && brief[6] == 2 && text[5] == 18);
  assert(std::memcmp(brief+28, "W1AW", 5)==0);
  assert(std::memcmp(brief+44, "FN31", 5)==0);
  assert(std::memcmp(text+20, "CQ W1AW FN31", 13)==0);
  assert((brief[54] & dial_preview::grid_associated)!=0);
  assert((brief[54] & dial_preview::snr_valid)!=0);
  assert(brief[53] == uint8_t(int8_t(-12)));
  const auto crc = dial_preview::crc32(brief,60);
  assert(brief[60]==uint8_t(crc) && brief[61]==uint8_t(crc>>8));
  d.kind=DecodeKind::qso;
  assert(dial_preview::encode(d,123,809,brief,text));
  assert((brief[54] & dial_preview::grid_associated)==0);
  assert(brief[44]==0); // no false location for an unassociated grid
  d.flags=decode_flag_snr_unavailable;
  assert(dial_preview::encode(d,123,810,brief,text));
  assert((brief[54] & dial_preview::snr_valid)==0);
  assert(brief[53]==0);
  d.dial_hz=0;
  assert(!dial_preview::encode(d,123,811,brief,text));
  d.dial_hz=14074000;
  d.callsign[0]=1; // nonprintable input must never escape
  assert(!dial_preview::encode(d,123,811,brief,text));
  std::puts("ft8_dial_preview_tests PASS");
}
