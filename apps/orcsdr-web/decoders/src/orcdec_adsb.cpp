// ADS-B adapter: OrcSDR's Mode S demodulator (adsb_decoder) behind the orcdec ABI, plus the per-aircraft position
// tracking OrcSDR's main.cpp does (even/odd CPR frames of one aircraft within 10 s give a global position).
//
// Input: CU8 IQ at 2 048 000 S/s centred on 1090 MHz. Time inside the decoder is the sample count, so results do not
// depend on the wall clock or on how the input is chunked into calls (a CPR pair's age is measured at the end of the
// call that delivered its second frame, so a chunk boundary can move that by at most one chunk, about 8 ms at 16 KB).
#include "orcdec_internal.hpp"
#include "adsb_decoder.hpp"

#include <map>


namespace {

using namespace orcsdr::adsb_rx;

struct Track {
  Frame even, odd;
  uint64_t even_ms = 0, odd_ms = 0;
  bool has_even = false, has_odd = false;
};

struct Adsb : orcdec {
  explicit Adsb(const char*) { decoder_.reset(); }

  void reset() override {
    decoder_.reset();
    tracks_.clear();
    samples_ = 0;
  }

  static void on_frame(const Frame& f, void* ctx) { static_cast<Adsb*>(ctx)->frame(f); }

  void frame(const Frame& f) {
    char icao[8];
    snprintf(icao, sizeof icao, "%06X", (unsigned)f.icao);
    char raw[29] = {};
    for (size_t i = 0; i < f.bit_length / 8u; ++i) snprintf(raw + i * 2, sizeof raw - i * 2, "%02X", f.bytes[i]);
    Json j;
    j.str("decoder", "adsb").str("type", "frame").unum("seq", sequence++).unum("df", f.bytes[0] >> 3).str("icao", icao)
        .unum("bits", f.bit_length).str("raw", raw).unum("signal", f.signal);
    if (f.bit_length == 112) j.unum("tc", f.type_code);
    if (f.has_callsign) j.str("callsign", f.callsign);
    if (f.has_altitude) j.num("alt_ft", f.altitude_ft);
    if (f.has_speed) j.num("speed_kts", f.speed_kts);
    if (f.has_heading) j.num("heading_deg", f.heading_deg);
    if (f.has_vertical_rate) j.num("vrate_fpm", f.vertical_rate_fpm);
    if (f.has_cpr) j.boolean("odd", f.cpr_odd).unum("cpr_lat", f.cpr_latitude).unum("cpr_lon", f.cpr_longitude);
    push(j.done());
    if (!f.has_cpr) return;
    Track& t = tracks_[f.icao];
    const uint64_t now_ms = samples_ * 1000ull / 2048000ull;
    if (f.cpr_odd) { t.odd = f; t.odd_ms = now_ms; t.has_odd = true; }
    else { t.even = f; t.even_ms = now_ms; t.has_even = true; }
    if (!t.has_even || !t.has_odd) return;
    const uint64_t age = t.even_ms > t.odd_ms ? t.even_ms - t.odd_ms : t.odd_ms - t.even_ms;
    if (age > 10000) return;
    double lat = 0, lon = 0;
    if (!decode_global_cpr(t.even, t.odd, t.odd_ms > t.even_ms, &lat, &lon)) return;
    Json p;
    p.str("decoder", "adsb").str("type", "position").unum("seq", sequence++).str("icao", icao).flt("lat", lat, 5).flt("lon", lon, 5);
    if (f.has_altitude) p.num("alt_ft", f.altitude_ft);
    push(p.done());
  }

  int feed(const void* data, size_t bytes) override {
    if (bytes % 2) return -1;
    samples_ += bytes / 2;
    decoder_.process_cu8(static_cast<const uint8_t*>(data), bytes, on_frame, this);
    return 0;
  }

  int command(const char* cmd, uint64_t) override {
    if (!strcmp(cmd, "finish")) {
      const Stats& s = decoder_.stats();
      Json j;
      // The decoder's preamble/frame/df17 counters re-count candidates in the samples it keeps between calls, so they depend on
      // how the input was chunked; they are left out so that events do not.
      j.str("decoder", "adsb").str("type", "summary").unum("seq", sequence++).unum("crc_ok", s.crc_ok)
          .unum("magnitude_min", s.magnitude_min).unum("magnitude_max", s.magnitude_max).unum("aircraft", tracks_.size());
      push(j.done());
      return 0;
    }
    if (!strcmp(cmd, "slot_begin")) return 0;
    return -1;
  }

  Decoder decoder_;
  std::map<uint32_t, Track> tracks_;
  uint64_t samples_ = 0;
};

}  // namespace

orcdec* orcdec_make_adsb(const char* options) { return new (std::nothrow) Adsb(options); }
