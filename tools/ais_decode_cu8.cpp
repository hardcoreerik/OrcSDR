// Host reference run of the OrcSDR AIS receive pipeline over a CU8 IQ file (240 kS/s, centred on 162.000 MHz).
// Feeds the file in the same block size the Tab5's USB path delivers (32768 bytes) and prints every CRC-valid packet
// plus the same counters and packet digest the firmware reports in RTL_AIS STATUS, so a device replay of the same file
// can be compared exactly.
//
//   U=apps/orcsdr-tab5/ui
//   g++ -std=c++17 -O2 -I$U tools/ais_decode_cu8.cpp $U/ais_radio.cpp $U/ais_receiver.cpp $U/ais_receive_pipeline.cpp
//       $U/ais_protocol.cpp $U/ais_binary_app.cpp $U/ais_station_store.cpp $U/ais_snapshot_adapter.cpp -o ais_decode_cu8
//   ./ais_decode_cu8 ais_fleet.cu8 [block_bytes] [loops]
//
// Output: one "PKT" line per packet (sample time, channel, bits, type, id, position when decoded, payload hex) and one
// "SUMMARY" line. tools/ais_check_replay.py compares the PKT lines with the generator's manifest.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "ais_radio.hpp"
#include "ais_receive_pipeline.hpp"

namespace {
using namespace orcsdr;

struct Run {
  uint64_t samples_before_block = 0;
  uint64_t packets = 0, digest = 0;
  uint64_t by_type[ais_radio::kTypeSlots]{};
  uint64_t by_channel[3]{};
};

void on_packet(const ais_rx::Packet& packet, const ais::Message* parsed, void* context) {
  Run* run = static_cast<Run*>(context);
  ++run->packets;
  run->digest += ais_radio::packet_hash(packet);
  const unsigned type = parsed ? parsed->type : 0;
  ++run->by_type[type < ais_radio::kTypeSlots ? type : 0];
  ++run->by_channel[packet.channel <= 2 ? packet.channel : 0];
  std::printf("PKT block_start_s=%.3f ch=%u bits=%u type=%u id=%u", run->samples_before_block / 240000.0,
              static_cast<unsigned>(packet.channel), static_cast<unsigned>(packet.bit_count), type,
              parsed ? static_cast<unsigned>(parsed->source_id) : 0u);
  if (parsed && parsed->has_position)
    std::printf(" lat_e7=%d lon_e7=%d", static_cast<int>(parsed->latitude_e7), static_cast<int>(parsed->longitude_e7));
  if (parsed && parsed->has_speed) std::printf(" sog_tenths=%u", static_cast<unsigned>(parsed->speed_tenths_kn));
  if (parsed && parsed->has_course) std::printf(" cog_tenths=%u", static_cast<unsigned>(parsed->course_tenths_deg));
  if (parsed && parsed->has_heading) std::printf(" heading=%u", static_cast<unsigned>(parsed->heading_deg));
  if (parsed && parsed->has_nav_status) std::printf(" nav=%u", static_cast<unsigned>(parsed->nav_status));
  if (parsed) {
    auto text = [](const char* key, const char* value) {   // spaces become '_' so the line stays key=value
      std::printf(" %s=", key);
      for (const char* c = value; *c; ++c) std::putchar(*c == ' ' ? '_' : *c);
    };
    const ais::VesselStatic& v = parsed->vessel;
    if (v.has_name) text("name", v.name);
    if (parsed->coverage == ais::Coverage::aid21 && parsed->aid.name[0]) text("name", parsed->aid.name);
    if (v.has_callsign) text("callsign", v.callsign);
    if (v.has_destination) text("dest", v.destination);
    if (v.has_ship_type) std::printf(" ship_type=%u", static_cast<unsigned>(v.ship_type));
    if (v.has_dimensions)
      std::printf(" dims=%u/%u/%u/%u", static_cast<unsigned>(v.to_bow_m), static_cast<unsigned>(v.to_stern_m),
                  static_cast<unsigned>(v.to_port_m), static_cast<unsigned>(v.to_starboard_m));
    if (v.imo) std::printf(" imo=%u", static_cast<unsigned>(v.imo));
  }
  std::printf(" hex=");
  for (size_t i = 0; i < (packet.bit_count + 7u) / 8u; ++i) std::printf("%02X", packet.bytes[i]);
  std::printf("\n");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s file.cu8 [block_bytes=32768] [loops=1]\n", argv[0]);
    return 2;
  }
  const size_t block = argc > 2 ? static_cast<size_t>(std::strtoul(argv[2], nullptr, 10)) & ~size_t{1} : 32768;
  const unsigned loops = argc > 3 ? static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10)) : 1;
  std::FILE* f = std::fopen(argv[1], "rb");
  if (!f || block == 0) { std::perror(argv[1]); return 2; }
  auto pipeline = std::make_unique<ais_rx::Pipeline>();
  Run run;
  pipeline->set_packet_observer(on_packet, &run);
  std::vector<uint8_t> buffer(block);
  for (unsigned loop = 0; loop < loops; ++loop) {
    std::rewind(f);
    size_t got;
    while ((got = std::fread(buffer.data(), 1, block, f)) > 0) {
      got &= ~size_t{1};
      // Time is derived from the sample count, so the run is the same on any machine.
      const uint64_t now_ms = run.samples_before_block * 1000u / 240000u + 1u;
      (void)pipeline->offer(buffer.data(), got, 240000, 162000000, 1, now_ms);
      run.samples_before_block += got / 2;
    }
  }
  std::fclose(f);
  const ais_rx::PipelineStats s = pipeline->stats();
  std::printf("SUMMARY samples=%" PRIu64 " packets=%" PRIu64 " ch1=%" PRIu64 " ch2=%" PRIu64 " ch1_ok=%" PRIu64 " ch2_ok=%" PRIu64
              " dup=%" PRIu64 " decoded=%" PRIu64 " header_only=%" PRIu64 " parse_rejected=%" PRIu64 " station_updates=%" PRIu64
              " stations=%zu digest=%016" PRIx64 " types=",
              run.samples_before_block, run.packets, run.by_channel[1], run.by_channel[2], s.rf.channels[0].crc_ok,
              s.rf.channels[1].crc_ok, s.rf.channels[0].duplicates + s.rf.channels[1].duplicates, s.messages_decoded,
              s.messages_header_only, s.parse_rejected, s.station_updates, pipeline->stations().size(), run.digest);
  bool any = false;
  for (unsigned type = 0; type < ais_radio::kTypeSlots; ++type) {
    if (!run.by_type[type]) continue;
    std::printf("%s%u:%" PRIu64, any ? "," : "", type, run.by_type[type]);
    any = true;
  }
  std::printf("\n");
  return 0;
}
