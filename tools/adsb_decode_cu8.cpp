// Decode a CU8 file (2.048 MS/s) with the Tab5's own ADS-B decoder, on the host. Used to prove a synthetic or recorded
// capture decodes before it is sent over rtl_tcp.
//   g++ -O2 -std=c++17 -I apps/orcsdr-tab5/ui tools/adsb_decode_cu8.cpp apps/orcsdr-tab5/ui/adsb_decoder.cpp -o adsb_decode_cu8
//   ./adsb_decode_cu8 adsb_2048k.cu8
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "adsb_decoder.hpp"

struct Tally {
  unsigned frames = 0, with_callsign = 0, with_position = 0, with_velocity = 0;
  std::set<unsigned> icaos;
  std::set<std::string> callsigns;
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s file.cu8\n", argv[0]); return 2; }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) { std::perror("open"); return 2; }
  orcsdr::adsb_rx::Decoder decoder;
  decoder.reset();
  Tally tally;
  std::vector<uint8_t> buffer(32768);
  size_t n;
  while ((n = std::fread(buffer.data(), 1, buffer.size(), f)) > 0) {
    decoder.process_cu8(buffer.data(), n, [](const orcsdr::adsb_rx::Frame& frame, void* context) {
      auto* t = static_cast<Tally*>(context);
      ++t->frames;
      t->icaos.insert(frame.icao);
      if (frame.has_callsign) { ++t->with_callsign; t->callsigns.insert(frame.callsign); }
      if (frame.has_cpr) ++t->with_position;
      if (frame.has_speed) ++t->with_velocity;
    }, &tally);
  }
  std::fclose(f);
  const auto& s = decoder.stats();
  std::printf("frames=%u df17=%u crc_ok=%u preambles=%u aircraft=%zu callsigns=%zu position=%u velocity=%u\n",
              tally.frames, s.df17, s.crc_ok, s.preambles, tally.icaos.size(), tally.callsigns.size(),
              tally.with_position, tally.with_velocity);
  for (const auto& c : tally.callsigns) std::printf("callsign %s\n", c.c_str());
  return 0;
}
