// Scanner benchmark harness (host). Real captures, optionally degraded with added white Gaussian noise, through the production chain.
//   dv_bench file <cu8> <channel_hz> <mask> [added_noise_db] [seed]   added_noise_db is relative to the capture's own RMS (0 = equal power)
//   dv_bench noise <seconds> <mask> [rms_counts] [seed]               pure noise at a receiver-like level: false-identification test
// Prints one JSON object: first_event_s / first_identity_s (stream time from the start, 0.7 s retune settle excluded), events, identity events, ids.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "dv_radio.hpp"
using namespace orcsdr;
static uint32_t g_now = 1000;
static bool ready() { return true; }
static bool tune(uint32_t, uint32_t) { return true; }
static void release() {}
static uint32_t now() { return g_now; }
static uint32_t us() { return g_now * 1000u; }
static uint64_t lost() { return 0; }
struct Out { unsigned events = 0, ident = 0; double first_event = -1, first_ident = -1; std::set<std::string> ids; };
static void start_chain(uint32_t hz, int mask) {
  g_now = 1000;
  dv_radio::Hooks h; h.device_ready = ready; h.tune = tune; h.release = release; h.now_ms = now; h.micros = us; h.lost_samples = lost;
  dv_radio::install(h);
  dv_radio::start();
  dv_radio::set_protocol_mask(static_cast<uint8_t>(mask));
  dv_radio::tune_channel(hz);
  g_now += 700;
}
static void drain(Out& o, double stream_s) {
  dv_radio::Event e;
  while (dv_radio::pop_event(&e)) {
    ++o.events; if (o.first_event < 0) o.first_event = stream_s;
    if (e.evidence == digital_voice_ui::Evidence::identity_valid) {
      ++o.ident; if (o.first_ident < 0) o.first_ident = stream_s;
      char k[64]; std::snprintf(k, sizeof k, "%u>%u@%u", e.source_id, e.destination_id, e.colour_code); o.ids.insert(k);
    }
  }
}
static void feed(Out& o, const uint8_t* d, size_t n, double& t) {
  dv_radio::offer_iq(d, n, 240000);
  g_now += static_cast<uint32_t>(n / 2 * 1000 / 240000);
  t += n / 2.0 / 240000.0;
  drain(o, t);
}
static void print(const char* what, const Out& o, double secs, double noise_db) {
  std::string ids; for (auto& s : o.ids) ids += (ids.empty() ? "" : ",") + s;
  std::printf("{\"mode\":\"%s\",\"seconds\":%.1f,\"added_noise_db\":%s,\"events\":%u,\"identity_events\":%u,\"first_event_s\":%.2f,\"first_identity_s\":%.2f,\"ids\":\"%s\"}\n",
              what, secs, std::isfinite(noise_db) ? std::to_string(noise_db).c_str() : "null", o.events, o.ident, o.first_event, o.first_ident, ids.c_str());
}
int main(int argc, char** argv) {
  if (argc < 4) return 2;
  Out o; double t = 0;
  if (!std::strcmp(argv[1], "file")) {
    if (argc < 5) return 2;
    FILE* f = std::fopen(argv[2], "rb"); if (!f) return 3;
    const uint32_t hz = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)); const int mask = std::atoi(argv[4]);
    const double db = argc > 5 ? std::atof(argv[5]) : -999.0; const unsigned seed = argc > 6 ? static_cast<unsigned>(std::atoi(argv[6])) : 1;
    std::vector<uint8_t> all; { uint8_t b[65536]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) all.insert(all.end(), b, b + n); }
    std::fclose(f);
    double sum = 0; for (size_t i = 0; i < all.size(); ++i) { const double v = all[i] - 127.5; sum += v * v; }
    const double rms = std::sqrt(sum / all.size());
    const double sigma = db > -900 ? rms * std::pow(10.0, db / 20.0) : 0.0;
    std::mt19937 rng(seed); std::normal_distribution<double> nd(0.0, 1.0);
    if (sigma > 0) for (auto& v : all) { double x = v + sigma * nd(rng); v = static_cast<uint8_t>(x < 0 ? 0 : x > 255 ? 255 : x + 0.5); }
    start_chain(hz, mask);
    for (size_t p = 0; p + 16384 <= all.size() || p < all.size(); p += 16384) feed(o, all.data() + p, std::min<size_t>(16384, all.size() - p), t);
    for (int i = 0; i < 20; ++i) { g_now += 50; drain(o, t); }
    dv_radio::stop();
    print("file", o, all.size() / 480000.0, db > -900 ? db : std::nan(""));
  } else {   // noise
    const double secs = std::atof(argv[2]); const int mask = std::atoi(argv[3]);
    const double rms = argc > 4 ? std::atof(argv[4]) : 25.0; const unsigned seed = argc > 5 ? static_cast<unsigned>(std::atoi(argv[5])) : 1;
    std::mt19937 rng(seed); std::normal_distribution<double> nd(0.0, rms);
    start_chain(454125000, mask);
    std::vector<uint8_t> b(16384);
    const size_t blocks = static_cast<size_t>(secs * 480000.0 / b.size());
    for (size_t k = 0; k < blocks; ++k) { for (auto& v : b) { double x = 127.5 + nd(rng); v = static_cast<uint8_t>(x < 0 ? 0 : x > 255 ? 255 : x + 0.5); } feed(o, b.data(), b.size(), t); }
    dv_radio::stop();
    print("noise", o, t, std::nan(""));
  }
  return 0;
}
