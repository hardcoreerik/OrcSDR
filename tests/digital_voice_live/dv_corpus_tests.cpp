// Permanent regression corpus: real RTL-SDR captures through the production dv_radio chain, compared against frozen expectations
// (tests/digital_voice_corpus/manifest.txt). No synthetic data. Identity sets are compared exactly; event counts are lower bounds.
// usage: dv_corpus_tests <corpus_dir> [--print]    --print shows measured values instead of failing (to freeze new cases)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
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

struct Result { unsigned events = 0, identity_events = 0; std::set<std::string> ids; };

static bool run_case(const std::string& path, uint32_t hz, int mask, Result& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  g_now = 1000;
  dv_radio::Hooks h; h.device_ready = ready; h.tune = tune; h.release = release; h.now_ms = now; h.micros = us; h.lost_samples = lost;
  dv_radio::install(h);
  if (!dv_radio::start()) { std::fclose(f); return false; }
  dv_radio::set_protocol_mask(static_cast<uint8_t>(mask));
  dv_radio::tune_channel(hz);
  g_now += 700;
  std::vector<uint8_t> buf(16384);
  size_t n;
  dv_radio::Event e;
  auto drain = [&] {
    while (dv_radio::pop_event(&e)) {
      ++out.events;
      if (e.evidence == digital_voice_ui::Evidence::identity_valid) {
        ++out.identity_events;
        char k[96];
        std::snprintf(k, sizeof k, "%u>%u@%u", e.source_id, e.destination_id, e.colour_code);
        out.ids.insert(k);
      }
    }
  };
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
    dv_radio::offer_iq(buf.data(), n, 240000);
    g_now += static_cast<uint32_t>(n / 2 * 1000 / 240000);
    drain();
  }
  for (int i = 0; i < 20; ++i) { g_now += 50; drain(); }
  dv_radio::stop();
  std::fclose(f);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::puts("usage: dv_corpus_tests <corpus_dir> [--print]"); return 2; }
  const std::string dir = argv[1];
  const bool print = argc > 2 && std::strcmp(argv[2], "--print") == 0;
  std::ifstream mf(dir + "/manifest.txt");
  if (!mf) { std::puts("FAIL cannot open manifest"); return 2; }
  std::string line; int cases = 0, failed = 0;
  while (std::getline(mf, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag, name, file, ids; unsigned long hz; int mask; unsigned min_events, max_ident;
    if (!(ss >> tag >> name >> file >> hz >> mask >> min_events >> max_ident >> ids) || tag != "case") { std::printf("FAIL bad manifest line: %s\n", line.c_str()); return 2; }
    std::set<std::string> want;
    if (ids != "-") { std::stringstream is(ids); std::string t; while (std::getline(is, t, ',')) want.insert(t); }
    Result r;
    ++cases;
    if (!run_case(dir + "/" + file, static_cast<uint32_t>(hz), mask, r)) { std::printf("FAIL %s: cannot run\n", name.c_str()); ++failed; continue; }
    std::string got; for (auto& s : r.ids) got += (got.empty() ? "" : ",") + s;
    if (print) { std::printf("case %s %s %lu %d %u %u %s   # measured events=%u identity_events=%u\n", name.c_str(), file.c_str(), hz, mask, r.events, r.identity_events, got.empty() ? "-" : got.c_str(), r.events, r.identity_events); continue; }
    bool ok = r.events >= min_events && r.identity_events <= max_ident && r.ids == want;
    std::printf("%s %s: events=%u (>=%u) identity_events=%u (<=%u) ids=[%s] expected=[%s]\n", ok ? "PASS" : "FAIL", name.c_str(), r.events, min_events, r.identity_events, max_ident, got.c_str(), ids.c_str());
    if (!ok) ++failed;
  }
  std::printf("%s dv_corpus_tests: %d cases, %d failed\n", failed ? "FAIL" : "PASS", cases, failed);
  return failed ? 1 : 0;
}
