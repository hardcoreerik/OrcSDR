// Decode a raw CU8 IQ file with OrcSDR's own ADS-B decoder (ui/adsb_decoder.cpp), unchanged.
//
// Lets a PC receiver (rtl_sdr.exe writing IQ) be scored with the exact code the Tab5 runs, so differences
// between the two come from the dongle, antenna and site, not from the decoder. Also usable to replay any
// IQ capture (the Tab5's .orciq payload is CU8 too) through the decoder.
//
//   g++ -O2 -std=c++17 -I../../ui adsb_decode_cu8.cpp ../../ui/adsb_decoder.cpp -o adsb_decode_cu8
//   adsb_decode_cu8 capture.cu8 [sample_rate_hz]
//
// Output: CSV "t,icao,df,tc,altitude_ft,signal", one row per CRC-valid frame; t is seconds into the file
// (resolution of one 0.1 s chunk).  The last line, prefixed '#', carries the decoder's own counters.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "adsb_decoder.hpp"

namespace {

struct Context {
  double chunk_start_s = 0.0;
};

void on_frame(const orcsdr::adsb_rx::Frame& frame, void* raw) {
  const auto* ctx = static_cast<const Context*>(raw);
  const unsigned df = frame.bytes[0] >> 3;
  std::printf("%.2f,%06X,%u,%u,%d,%u\n", ctx->chunk_start_s, static_cast<unsigned>(frame.icao), df,
              static_cast<unsigned>(frame.type_code), frame.has_altitude ? frame.altitude_ft : 0,
              static_cast<unsigned>(frame.signal));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s capture.cu8 [sample_rate_hz]\n", argv[0]);
    return 2;
  }
  const double rate = argc > 2 ? std::atof(argv[2]) : 2048000.0;  // the decoder's bit timing is built for 2.048 MS/s
  std::FILE* file = std::fopen(argv[1], "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  const size_t chunk_samples = static_cast<size_t>(rate / 10.0);  // 0.1 s
  std::vector<uint8_t> buffer(chunk_samples * 2);
  static orcsdr::adsb_rx::Decoder decoder;
  decoder.reset();
  Context ctx;
  std::printf("t,icao,df,tc,altitude_ft,signal\n");
  uint64_t samples = 0;
  for (;;) {
    const size_t got = std::fread(buffer.data(), 1, buffer.size(), file);
    if (got < 2) break;
    ctx.chunk_start_s = static_cast<double>(samples) / rate;
    decoder.process_cu8(buffer.data(), got, on_frame, &ctx);
    samples += got / 2;
  }
  std::fclose(file);
  const auto& s = decoder.stats();
  std::printf("# seconds=%.1f preambles=%u frames=%u df17=%u crc_ok=%u\n", static_cast<double>(samples) / rate,
              s.preambles, s.frames, s.df17, s.crc_ok);
  return 0;
}
