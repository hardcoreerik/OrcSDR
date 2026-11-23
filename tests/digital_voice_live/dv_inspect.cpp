// Host inspection tool: CU8 240 kS/s capture (channel +60 kHz above centre) -> channelizer -> D1 receiver, printing the RAW
// decoder events (sync kind, slot, data type, evidence, FLCO, ids) and a tally. For diagnosing what a capture really contains.
// usage: dv_inspect <file.cu8> [max_print]
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "dv_channelizer.hpp"
#include "orcsdr_dmr/d1_bridge.hpp"
namespace d1i = orcsdr::dmr::d1::integration;
namespace d1 = orcsdr::dmr::d1;
static std::map<std::tuple<int,int,int,int,int,int>, int> tally;
static unsigned printed = 0, max_print = 30;
static void on_ev(const d1i::Event& e, void*) noexcept {
  const d1::Event& d = e.decoded;
  tally[{int(d.sync), d.slot, d.data_type, int(d.evidence), d.flco, d.integrity_valid ? 1 : 0}]++;
  if (printed < max_print && (d.integrity_valid || d.evidence >= d1::Evidence::fec_valid)) {
    ++printed;
    std::printf("  sync=%d slot=%u cc=%u dt=%u evid=%d integ=%d flco=%u fid=%u opt=%02x src=%u dst=%u ident=%d late=%d\n", int(d.sync), d.slot,
                d.colour_code, d.data_type, int(d.evidence), d.integrity_valid, d.flco, d.feature_id, d.service_options, d.source_id,
                d.destination_id, int(d.identity), d.late_entry);
  }
}
int main(int argc, char** argv) {
  if (argc < 2) return 2;
  if (argc > 2) max_print = unsigned(std::atoi(argv[2]));
  FILE* f = std::fopen(argv[1], "rb"); if (!f) return 3;
  static orcsdr::dv::Channelizer ch; static d1i::Receiver rx;
  std::vector<uint8_t> in(2 * 4800); std::vector<orcsdr::dv::Iq48> out(4800 / 5 + 8); std::vector<orcsdr::dmr::d1::ComplexSample> c(out.size());
  size_t n;
  while ((n = std::fread(in.data(), 1, in.size(), f)) > 1) {
    size_t m = ch.process_cu8(in.data(), n / 2, out.data(), out.size());
    for (size_t i = 0; i < m; ++i) c[i] = {out[i].re, out[i].im};
    rx.process(c.data(), m, on_ev, nullptr);
  }
  std::printf("tally (sync,slot,datatype,evidence,flco,integ) -> count\n");
  for (auto& kv : tally) { auto [s,sl,dt,ev,fl,ig] = kv.first;
    std::printf("  sync=%d slot=%d dt=%d evid=%d flco=%d integ=%d : %d\n", s, sl, dt, ev, fl, ig, kv.second); }
  return 0;
}
