// Native Digital Voice audio: embedded-LC layout conformance, call/privacy gate, speech engine and corpus acceptance.
// Real IQ drives the corpus section; the gate scenarios are synthetic structures (no RF), clearly labelled.
#undef NDEBUG
#include <cmath>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "dv_radio.hpp"
#include "dv_voice_audio.hpp"
#include "dv_voice_service.hpp"
#include "dv_speech_rec.hpp"
#include "dv_scanner.hpp"
#include "dv_iq_rec.hpp"
#include "dv_bandplan.hpp"
#include "orcsdr_dmr/d1.hpp"
#include "orcsdr_dmr/d1_call.hpp"

using namespace orcsdr;
namespace d1 = dmr::d1;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- Embedded LC conformance (ETSI TS 102 361-1 B.2.2.1), independent encoder written from the layout description ----
namespace d1impl { bool decode_embedded_lc_public(const std::array<std::uint8_t, 128>&, std::array<std::uint8_t, 9>&); }
static const uint8_t kRow16[11] = {19,26,31,28,14,21,11,22,25,13,7};
static void encode_lc(const uint8_t lc[9], std::array<uint8_t, 128>& wire, bool old_layout, int checksum_delta = 0) {
  uint8_t m[8][16] = {};
  unsigned sum = 0;
  for (int i = 0; i < 9; ++i) sum += lc[i];
  unsigned chk = (sum + checksum_delta) % 31U;
  uint8_t bits[72];
  for (int i = 0; i < 72; ++i) bits[i] = (lc[i / 8] >> (7 - i % 8)) & 1;
  if (!old_layout) {
    int n = 0;
    for (int r = 0; r < 2; ++r) for (int c = 0; c < 11; ++c) m[r][c] = bits[n++];
    for (int r = 2; r < 7; ++r) { for (int c = 0; c < 10; ++c) m[r][c] = bits[n++]; m[r][10] = (chk >> (6 - r)) & 1; }
  } else {
    uint8_t flat[77];
    for (int i = 0; i < 72; ++i) flat[i] = bits[i];
    for (int i = 0; i < 5; ++i) flat[72 + i] = (chk >> (4 - i)) & 1;
    for (int i = 0; i < 77; ++i) m[i / 11][i % 11] = flat[i];
  }
  for (int r = 0; r < 7; ++r) {
    unsigned syn = 0;
    for (int c = 0; c < 11; ++c) if (m[r][c]) syn ^= kRow16[c];
    for (int k = 0; k < 5; ++k) m[r][11 + k] = (syn >> (4 - k)) & 1;
  }
  for (int c = 0; c < 16; ++c) { unsigned p = 0; for (int r = 0; r < 7; ++r) p ^= m[r][c]; m[7][c] = p; }
  for (int i = 0; i < 128; ++i) wire[i] = m[i % 8][i / 8];
}

// d1.cpp keeps decode_embedded_lc private; exercise it through a Decoder fed a synthetic superframe.
// Simplest faithful route: build four embedded bursts' worth of fragments by feeding real decoder state is not possible here,
// so the conformance test uses the public voice path in the corpus section and this encoder checks structure invariants.
static bool row_ok(const std::array<uint8_t, 128>& w) {
  uint8_t m[8][16];
  for (int i = 0; i < 128; ++i) m[i % 8][i / 8] = w[i];
  for (int r = 0; r < 7; ++r) { unsigned s = 0; for (int c = 0; c < 11; ++c) if (m[r][c]) s ^= kRow16[c]; for (int k = 0; k < 5; ++k) if (m[r][11 + k]) s ^= 1U << (4 - k); if (s) return false; }
  for (int c = 0; c < 16; ++c) { unsigned p = 0; for (int r = 0; r < 8; ++r) p ^= m[r][c]; if (p) return false; }
  return true;
}

// ---- Call manager scenarios -----------------------------------------------------------------------------------
struct Sink {
  std::vector<d1::QualifiedGroup> g; std::vector<d1::Notice> n;
};
static void on_g(const d1::QualifiedGroup& q, void* u) noexcept { auto* s = static_cast<Sink*>(u); d1::QualifiedGroup c = q; c.group = nullptr; s->g.push_back(c); }
static void on_n(const d1::Notice& n, void* u) noexcept { static_cast<Sink*>(u)->n.push_back(n); }
static d1::Event data_ev(uint64_t t, uint8_t slot, uint8_t cc, uint8_t type, uint32_t src = 0, uint32_t dst = 0, uint8_t svc = 0, bool pf = false) {
  d1::Event e; e.burst_start_dibit = t; e.sync = dmr::SyncKind::bs_data; e.slot = slot; e.slot_from_cach = slot != 0; e.colour_code = cc;
  e.golay_valid = true; e.data_type = type;
  if (src) { e.evidence = d1::Evidence::identity_valid; e.integrity_valid = true; e.source_id = src; e.destination_id = dst; e.service_options = svc; e.protected_flag = pf; e.flco = 0; e.identity = d1::IdentityKind::group; }
  return e;
}
static d1::VoiceGroup group(uint64_t t, uint8_t slot, uint8_t cc, uint32_t src = 0, uint32_t dst = 0, uint8_t b0 = 0, uint8_t svc = 0) {
  d1::VoiceGroup g; g.start_dibit = t; g.slot = slot; g.colour_code = cc;
  if (src) {
    g.lc_valid = true; g.lc[0] = b0; g.lc[1] = 0; g.lc[2] = svc;
    g.lc[3] = dst >> 16; g.lc[4] = dst >> 8; g.lc[5] = dst; g.lc[6] = src >> 16; g.lc[7] = src >> 8; g.lc[8] = src;
  }
  return g;
}
static void gate_tests() {
  const uint64_t SF = d1::CallManager::kSuperframeDibits;
  { // header opens a call; continuation, tolerated loss, too-long loss
    d1::CallManager m; Sink s; m.set_sinks(on_g, on_n, &s);
    m.on_data(data_ev(1000, 1, 3, 1, 4106011, 7));
    m.on_group(group(1288, 1, 3));
    CHECK(s.g.size() == 1 && s.g[0].new_call && s.g[0].source_id == 4106011 && s.g[0].slot == 1);
    CHECK(m.privacy(1) == d1::Privacy::clear);
    m.on_group(group(1288 + SF, 1, 3));
    CHECK(s.g.size() == 2 && !s.g[1].new_call && s.g[1].epoch == s.g[0].epoch && s.g[1].lost_superframes == 0);
    m.on_group(group(1288 + 4 * SF, 1, 3));   // two superframes missing, but the 2 s identity anchor is still fresh
    CHECK(s.g.size() == 3 && s.g[2].lost_superframes == 2);
    m.on_group(group(1288 + 10 * SF, 1, 3));  // anchor older than 2 s and no new evidence: not clear
    CHECK(s.g.size() == 3 && m.privacy(1) == d1::Privacy::unknown);
  }
  { // PI header: protected, nothing released, later evidence-free groups stay unqualified
    d1::CallManager m; Sink s; m.set_sinks(on_g, on_n, &s);
    m.on_data(data_ev(1000, 1, 3, 0));
    m.on_group(group(1288, 1, 3));
    CHECK(s.g.empty() && m.privacy(1) == d1::Privacy::protected_voice);
    m.on_group(group(1288 + SF, 1, 3));
    CHECK(s.g.empty());
    bool prot = false; for (auto& n : s.n) prot |= n.kind == d1::NoticeKind::protected_voice;
    CHECK(prot);
  }
  { // late entry from the embedded LC alone
    d1::CallManager m; Sink s; m.set_sinks(on_g, on_n, &s);
    m.on_group(group(5000, 2, 9, 4106019, 100));
    CHECK(s.g.size() == 1 && s.g[0].slot == 2 && s.g[0].destination_id == 100 && s.g[0].group_call);
    m.on_group(group(5000 + SF, 2, 9));
    CHECK(s.g.size() == 2 && !s.g[1].new_call);
  }
  { // protection indicators: LC protect flag, service-options privacy bit, EMB PI, voice header service bit
    for (int k = 0; k < 4; ++k) {
      d1::CallManager m; Sink s; m.set_sinks(on_g, on_n, &s);
      if (k == 0) m.on_group(group(2000, 1, 1, 5, 6, 0x80));
      if (k == 1) m.on_group(group(2000, 1, 1, 5, 6, 0, 0x40));
      if (k == 2) { auto g = group(2000, 1, 1, 5, 6); g.pi_seen = true; m.on_group(g); }
      if (k == 3) { m.on_data(data_ev(1712, 1, 1, 1, 5, 6, 0x40)); m.on_group(group(2000, 1, 1)); }
      CHECK(s.g.empty());
      CHECK(m.privacy(1) == d1::Privacy::protected_voice);
    }
  }
  { // no evidence at all, unknown slot, wrong colour code, conflicting identities
    d1::CallManager m; Sink s; m.set_sinks(on_g, on_n, &s);
    m.on_group(group(3000, 1, 1));
    m.on_group(group(3000 + SF, 1, 1));
    CHECK(s.g.empty() && m.privacy(1) == d1::Privacy::unknown);
    m.on_group(group(9000, 0, 1, 5, 6));
    CHECK(s.g.empty());
    m.on_data(data_ev(11000, 1, 4, 1, 5, 6));
    m.on_group(group(11288, 1, 4, 5, 7));   // header says 5->6, embedded LC says 5->7
    CHECK(s.g.empty());
    m.on_data(data_ev(20000, 1, 4, 1, 5, 6));
    m.on_group(group(20288, 1, 2));          // header colour code differs from the voice colour code
    CHECK(s.g.empty());
  }
  { // slots are independent; a data burst or terminator on one slot never touches the other
    d1::CallManager m; Sink s; m.set_sinks(on_g, on_n, &s);
    m.on_data(data_ev(1000, 1, 3, 1, 11, 1)); m.on_data(data_ev(1144, 2, 3, 1, 22, 2));
    m.on_group(group(1288, 1, 3)); m.on_group(group(1432, 2, 3));
    CHECK(s.g.size() == 2 && s.g[0].source_id == 11 && s.g[1].source_id == 22);
    m.on_data(data_ev(1288 + SF - 288, 2, 3, 2));        // TS2 terminator
    m.on_group(group(1288 + SF, 1, 3));
    m.on_group(group(1432 + SF, 2, 3));
    CHECK(s.g.size() == 3 && s.g[2].slot == 1 && !s.g[2].new_call);
    CHECK(!m.active(2));
    m.on_time(1288 + SF + 20 * SF);
    CHECK(!m.active(1));
  }
}

// ---- Speech engine ---------------------------------------------------------------------------------------------------
static dv_radio::VoiceMsg msg(uint8_t slot, uint32_t serial, bool nc, const char* frame_hex) {
  dv_radio::VoiceMsg m; m.kind = dv_radio::VoiceKind::group; m.slot = slot; m.call_serial = serial; m.new_call = nc; m.colour_code = 1;
  m.source_id = 1; m.destination_id = 2;
  uint8_t w[9];
  for (int i = 0; i < 9; ++i) { unsigned x; std::sscanf(frame_hex + 2 * i, "%2x", &x); w[i] = static_cast<uint8_t>(x); }
  for (int b = 0; b < 6; ++b) for (int f = 0; f < 3; ++f) std::memcpy(m.payload + b * 27 + f * 9, w, 9);
  return m;
}
static std::vector<int16_t> drain(dv_voice::Engine& e, uint32_t& now, uint32_t ms) {
  std::vector<int16_t> out, tmp(1024);
  for (uint32_t t = 0; t < ms; t += 10) { now += 10; size_t g = e.pull(tmp.data(), 480, now); out.insert(out.end(), tmp.begin(), tmp.begin() + g); }
  return out;
}
static void engine_tests() {
  using namespace dv_voice;
  uint32_t now = 1000;
  const char* kSpeech = "B9E881526173002A6B";    // taken from the real capture terra_153p068 (3 speech frames in that call)
  { // silence when nothing arrived, nothing playing before a group
    Engine e; CHECK(e.begin()); e.set_enabled(true);
    CHECK(e.pull(nullptr, 0, now) == 0);
    auto o = drain(e, now, 200); CHECK(o.empty());
    CHECK(e.snapshot(now).phase == Phase::listening);
  }
  { // enabled engine, selected slot: audio only after the jitter hold; correct sample count (18 frames x 960)
    Engine e; Config c; c.target_buffer_ms = 100; CHECK(e.begin(c)); e.set_enabled(true);
    e.handle(msg(1, 1, true, kSpeech), now);
    CHECK(e.buffered_samples() == 18u * 960u);
    CHECK(e.snapshot(now).phase == Phase::buffering);
    std::vector<int16_t> tmp(4800);
    CHECK(e.pull(tmp.data(), 4800, now + 50) == 0);                  // still inside the jitter hold
    CHECK(e.pull(tmp.data(), 4800, now + 100) == 4800);
    CHECK(e.snapshot(now + 100).phase != Phase::unavailable);
  }
  { // slot selection, mute, flush, protected notices
    Engine e; CHECK(e.begin()); e.set_enabled(true); e.set_listen(Listen::ts2);
    e.handle(msg(1, 1, true, kSpeech), now);
    CHECK(e.buffered_samples() == 0 && e.snapshot(now).stats.groups_dropped_not_selected == 1);
    e.handle(msg(2, 2, true, kSpeech), now);
    CHECK(e.buffered_samples() == 17280u);
    e.set_listen(Listen::mute);
    CHECK(e.buffered_samples() == 0);                                // changing the selection never keeps old audio
    e.handle(msg(2, 2, false, kSpeech), now);
    CHECK(e.buffered_samples() == 0 && e.snapshot(now).phase == Phase::muted);
    e.set_listen(Listen::automatic);
    e.handle(msg(1, 3, true, kSpeech), now);
    CHECK(e.buffered_samples() == 17280u);
    dv_radio::VoiceMsg fl; fl.kind = dv_radio::VoiceKind::flush; e.handle(fl, now);
    CHECK(e.buffered_samples() == 0);
    e.handle(msg(1, 4, true, kSpeech), now);
    dv_radio::VoiceMsg pr; pr.kind = dv_radio::VoiceKind::protected_voice; pr.slot = 1; e.handle(pr, now);
    CHECK(e.snapshot(now).phase == Phase::protected_voice && e.snapshot(now).privacy[1] == 2);
    e.handle(msg(1, 5, true, kSpeech), now);                         // a later clear group on the same slot is allowed again only through the manager
    e.set_muted(true); CHECK(e.buffered_samples() == 0);
  }
  { // deferred synthesis: nothing is voiced until service() runs, slices are bounded, a new group finishes the old one in order
    Engine e; CHECK(e.begin()); e.set_enabled(true);
    e.handle(msg(1, 1, true, kSpeech), now, true);
    CHECK(e.busy() && e.buffered_samples() == 0);
    CHECK(e.service(now, 3) == 3 && e.buffered_samples() == 3u * 960u && e.busy());
    e.handle(msg(1, 1, false, kSpeech), now + 10, true);                 // second group: the first completes (18 frames) before it starts
    CHECK(e.buffered_samples() == 18u * 960u && e.busy());
    while (e.busy()) (void)e.service(now + 20, 5);
    CHECK(e.buffered_samples() == 36u * 960u && e.snapshot(now).stats.groups == 2 && e.snapshot(now).stats.frames == 36);
    e.handle(msg(1, 1, false, kSpeech), now + 30, true);
    dv_radio::VoiceMsg fl; fl.kind = dv_radio::VoiceKind::flush; e.handle(fl, now + 31, true);   // a flush discards the half-built group
    CHECK(!e.busy() && e.buffered_samples() == 0);
  }
  { // AUTO sticks with the first voiced slot; lost superframes are silence; test PCM goes through the same path
    Engine e; CHECK(e.begin()); e.set_enabled(true);
    e.handle(msg(2, 1, true, kSpeech), now);
    e.handle(msg(1, 2, true, kSpeech), now);
    CHECK(e.snapshot(now).stats.groups_dropped_not_selected == 1 && e.snapshot(now).active_slot == 2);
    auto m = msg(2, 1, false, kSpeech); m.lost_superframes = 1;
    const size_t before = e.buffered_samples();
    e.handle(m, now);
    CHECK(e.buffered_samples() - before == 2u * 18u * 960u);
    CHECK(e.snapshot(now).stats.lost_superframes == 1);
    int16_t tone[160]; for (int i = 0; i < 160; ++i) tone[i] = static_cast<int16_t>(8000 * std::sin(2 * 3.14159265 * 1000.0 * i / 8000.0));
    Engine t; CHECK(t.begin()); t.set_enabled(true);
    CHECK(t.inject_test_pcm(tone, 160, now));
    CHECK(t.snapshot(now).stats.test_samples == 160 && t.snapshot(now).stats.frames == 0);   // not counted as decoding
    CHECK(t.buffered_samples() == 960);
  }
  { // resampler: a 1 kHz tone must survive 8->48 kHz at the right level and frequency (zero crossings), no clipping
    Engine t; Config c; c.target_buffer_ms = 0; CHECK(t.begin(c)); t.set_enabled(true);
    std::vector<int16_t> tone(160 * 20);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<int16_t>(8000 * std::sin(2 * 3.14159265 * 1000.0 * i / 8000.0));
    CHECK(t.inject_test_pcm(tone.data(), tone.size(), now));
    std::vector<int16_t> out(960 * 20); size_t g = t.pull(out.data(), out.size(), now + 1);
    CHECK(g == out.size());
    double sq = 0; int zc = 0; int peak = 0;
    for (size_t i = 960; i < out.size() - 960; ++i) { sq += double(out[i]) * out[i]; if ((out[i - 1] < 0) != (out[i] < 0)) ++zc; peak = std::max(peak, std::abs(int(out[i]))); }
    const double rms = std::sqrt(sq / (out.size() - 1920));
    const double secs = (out.size() - 1920) / 48000.0;
    CHECK(std::fabs(rms - 8000 / std::sqrt(2.0)) < 250.0);
    CHECK(std::fabs(zc / (2.0 * secs) - 1000.0) < 30.0);
    CHECK(peak < 8400 && t.snapshot(now).stats.clipped_samples == 0);
  }
}

// ---- Real-IQ corpus acceptance ---------------------------------------------------------------------------------------
static uint32_t g_now = 1000;
static bool ready() { return true; }
static bool tune(uint32_t, uint32_t) { return true; }
static void release() {}
static uint32_t nowf() { return g_now; }
static uint32_t usf() { return g_now * 1000u; }
static uint64_t lostf() { return 0; }
struct Corpus { unsigned groups = 0, calls = 0, frames = 0; unsigned protected_n = 0; unsigned flush = 0; uint32_t first_src = 0; uint8_t first_slot = 0; };
static bool run_corpus(const char* path, uint32_t hz, Corpus& c, bool queue_on = true) {
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  dv_radio::Hooks h; h.device_ready = ready; h.tune = tune; h.release = release; h.now_ms = nowf; h.micros = usf; h.lost_samples = lostf;
  g_now = 1000;
  dv_radio::install(h);
  if (!dv_radio::start()) return false;
  dv_radio::set_protocol_mask(dv_radio::kMaskDmr);
  dv_radio::set_voice_queue_enabled(queue_on);
  dv_radio::tune_channel(hz);
  g_now += 700;
  std::vector<uint8_t> buf(16384);
  size_t n; uint32_t last_serial = 0;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
    dv_radio::offer_iq(buf.data(), n, 240000);
    g_now += static_cast<uint32_t>(n / 2 * 1000 / 240000);
    dv_radio::VoiceMsg m;
    while (dv_radio::pop_voice(&m)) {
      if (m.kind == dv_radio::VoiceKind::group) {
        ++c.groups; c.frames += 18;
        if (m.call_serial != last_serial) { ++c.calls; last_serial = m.call_serial; }
        if (!c.first_src) { c.first_src = m.source_id; c.first_slot = m.slot; }
      } else if (m.kind == dv_radio::VoiceKind::protected_voice) ++c.protected_n;
      else if (m.kind == dv_radio::VoiceKind::flush) ++c.flush;
    }
  }
  std::fclose(f);
  dv_radio::stop();
  return true;
}
static void corpus_tests(const char* dir) {
  std::string d = dir;
  Corpus c;
  CHECK(run_corpus((d + "/iq/terra_152p1575_call_cc13.cu8").c_str(), 152157500, c));
  CHECK(c.groups == 26 && c.frames == 468 && c.calls == 2 && c.first_src == 4106011 && c.first_slot == 2 && c.protected_n == 0);
  Corpus c3;
  CHECK(run_corpus((d + "/iq/terra_153p068_call_cc8.cu8").c_str(), 153068000, c3));
  CHECK(c3.groups == 1 && c3.first_slot == 1 && c3.first_src == 4106008);
  for (const char* n : {"terra_152p1575_control_only", "negative_nxdn_voice_chan_idle_454p125", "capmax_451p9125_control_crcfail"}) {
    Corpus z;
    const uint32_t hz = std::strstr(n, "152p1575") ? 152157500u : std::strstr(n, "454p125") ? 454125000u : 451912500u;
    CHECK(run_corpus((d + "/iq/" + n + ".cu8").c_str(), hz, z));
    CHECK(z.groups == 0 && z.frames == 0);     // control/data-only and idle NXDN: never any speech
  }
  { // IQ capture end to end: a real call triggers it, the capture is 12 s of that stream
    dv_radio::iq_set_enabled(true);
    Corpus ic; CHECK(run_corpus((d + "/iq/terra_152p1575_call_cc13.cu8").c_str(), 152157500, ic));
    g_now += 12000;
    const uint8_t* data = nullptr; size_t bytes = 0; char name[64];
    CHECK(dv_radio::iq_ready(&data, &bytes, name, sizeof(name)) && bytes >= 10 * dv_iq_rec::kBytesPerSec && bytes <= dv_iq_rec::kRingBytes);   // 8 s after the first trigger plus whatever pre-roll existed
    CHECK(std::strstr(name, "_152157.cu8") != nullptr && dv_radio::iq_stats().triggers >= 1);
    dv_radio::iq_saving(); dv_radio::iq_release(true); dv_radio::iq_set_enabled(false);
  }
  Corpus off;
  CHECK(run_corpus((d + "/iq/terra_152p1575_call_cc13.cu8").c_str(), 152157500, off, false));
  CHECK(off.groups == 0);                         // queue disabled: counters only, nothing delivered
}

// ---- IQ capture (development) --------------------------------------------------------------------------------------------
static void iq_tests() {
  using namespace dv_iq_rec;
  Recorder r; CHECK(r.begin());
  std::vector<uint8_t> blk(kBytesPerSec / 10);                 // 100 ms of IQ
  auto feed = [&](int n, uint8_t v) { std::fill(blk.begin(), blk.end(), v); for (int i = 0; i < n; ++i) r.feed(blk.data(), blk.size()); };
  const uint8_t* d = nullptr; size_t n = 0; char name[64];
  feed(100, 1); r.trigger(1000, 152157500, 7); r.service(20000);
  CHECK(!r.ready(&d, &n, name, sizeof(name)));                 // disabled: nothing happens
  r.set_enabled(true);
  r.trigger(1000, 152157500, 7); CHECK(!r.busy());             // no pre-roll yet
  feed(50, 1);                                                 // 5 s of pre-roll
  r.trigger(2000, 152157500, 1795541906u); CHECK(r.busy());
  r.service(9000); CHECK(!r.ready(&d, &n, name, sizeof(name)));   // post-roll (8 s) still running
  feed(80, 2);                                                 // 8 s after the trigger
  r.service(10001);
  CHECK(r.ready(&d, &n, name, sizeof(name)) && n == kRingBytes);   // 12 s ring, full
  CHECK(std::strcmp(name, "/orcsdr/dviq_1795541906_152157.cu8") == 0);
  CHECK(d[0] == 1 && d[4 * kBytesPerSec - 1] == 1 && d[4 * kBytesPerSec] == 2);   // 4 s before the trigger, then 8 s after, oldest first
  CHECK(d[n - 1] == 2);                                        // newest last, post-roll included
  CHECK(r.ready(&d, &n, name, sizeof(name)) && d[n - 1] == 2); // idempotent until saving()
  const size_t before = n;
  r.saving(); feed(10, 9); CHECK(d[n - 1] == 2 && n == before);   // frozen: the card is reading the buffer
  r.trigger(10500, 1, 1); CHECK(r.stats().skipped_busy >= 1);
  r.release(true, 12000); CHECK(r.stats().saved == 1 && !r.busy());
  feed(30, 3); r.trigger(30000, 152157500, 5); CHECK(!r.busy() && r.stats().skipped_cooldown >= 1);   // 60 s cooldown
  feed(30, 3); r.trigger(80000, 152157500, 5); CHECK(r.busy());
  r.reset(); CHECK(!r.busy());                                  // an IQ discontinuity drops an armed capture
  r.end();
}

// ---- Scanner: multi-band plans, quick skip, control-only hold release ---------------------------------------------------
static void scanner_tests() {
  using namespace dv;
  { Scanner s;
    const Segment two[2] = {{150007500u, 12500u, 640}, {450000000u, 12500u, 1600}};
    CHECK(s.set_ranges(two, 2) && s.plan_count() == 2240 && s.plan_kind() == PlanKind::range);
    CHECK(s.plan_at(0) == 150007500u && s.plan_at(639) == 150007500u + 639u * 12500u && s.plan_at(640) == 450000000u && s.plan_at(2239) == 450000000u + 1599u * 12500u && s.plan_at(2240) == 0);
    const auto st = s.status();
    CHECK(st.segments == 2 && st.range_start_hz == 150007500u && st.range_end_hz == 450000000u + 1599u * 12500u);
    const Segment bad[2] = {{150007500u, 12500u, 10}, {100u, 12500u, 10}};
    CHECK(!s.set_ranges(bad, 2) && s.plan_count() == 2240);                       // one invalid segment changes nothing
    const Segment huge[1] = {{150000000u, 6250u, 9000}};
    CHECK(!s.set_ranges(huge, 1) && !s.set_ranges(nullptr, 0));
    Segment all[kBandPresetCount];
    size_t total = 0;
    for (size_t i = 0; i < kBandPresetCount; ++i) { all[i] = {kBandPresets[i].start_hz, kBandPresets[i].step_hz, static_cast<uint32_t>(channel_count(kBandPresets[i]))}; total += all[i].count; }
    CHECK(s.set_ranges(all, kBandPresetCount) && s.plan_count() == total && total <= kMaxRangeChannels);   // every built-in band at once fits
  }
  { // quick skip: a silent channel of a range plan is left after quick_ms, a list channel keeps the full dwell
    Scanner s; const Segment g[1] = {{152000000u, 12500u, 4}};
    CHECK(s.set_ranges(g, 1) && s.start(0));
    ScanInputs in; in.receiver_ready = true;
    ScanStep step = s.service(0, in); CHECK(step.tune && step.tune_hz == 152000000u);
    s.retune_result(true, 0); s.service(700, in);
    CHECK(s.status().state == ScanState::sampling);
    s.service(700 + 599, in); CHECK(s.status().index == 0);
    s.service(700 + 600, in); CHECK(s.status().index == 1 && s.status().quick_skips == 1);
    Scanner l; const uint32_t one[2] = {152000000u, 152012500u}; CHECK(l.set_plan(one, 2) && l.start(0));
    l.service(0, in); l.retune_result(true, 0); l.service(700, in); l.service(700 + 1000, in);
    CHECK(l.status().index == 0);                                                  // list plan: full 1500 ms dwell
    ScanInputs cand = in; cand.candidate = true;
    s.service(2000, in); s.retune_result(true, 2000);
  }
  { // control-only hold: verified events but no identity release the hold after 20 s; an identity keeps it
    Scanner s; const Segment g[1] = {{152000000u, 12500u, 4}}; CHECK(s.set_ranges(g, 1) && s.start(0));
    ScanInputs in; in.receiver_ready = true;
    s.service(0, in); s.retune_result(true, 0); s.service(700, in);
    in.verified_ms = 800; s.service(800, in); CHECK(s.status().state == ScanState::holding);
    for (uint32_t t = 1000; t < 20800; t += 500) { in.verified_ms = t; s.service(t, in); }
    CHECK(s.status().state == ScanState::holding && s.status().control_releases == 0);
    in.verified_ms = 20800; s.service(20800, in);
    CHECK(s.status().index == 1 && s.status().control_releases == 1);              // control traffic only: moved on
    Scanner c; CHECK(c.set_ranges(g, 1) && c.start(0));
    ScanInputs ci; ci.receiver_ready = true;
    c.service(0, ci); c.retune_result(true, 0); c.service(700, ci);
    ci.verified_ms = 800; ci.identity_ms = 900; c.service(800, ci);
    for (uint32_t t = 1000; t < 40000; t += 500) { ci.verified_ms = t; ci.identity_ms = t; c.service(t, ci); }
    CHECK(c.status().state == ScanState::holding && c.status().control_releases == 0);   // a real call is never cut off
    c.toggle_manual_hold(); c.service(60000, ci); CHECK(c.status().index == 0);          // manual hold never leaves either
  }
}

// ---- Speech recorder (decoded DMR speech only) -------------------------------------------------------------------
static void speech_tests() {
  using namespace dv_speech_rec;
  int16_t pcm[160]; for (int i = 0; i < 160; ++i) pcm[i] = static_cast<int16_t>(1000 + i);
  { Recorder r; CHECK(r.begin());
    for (int i = 0; i < 30; ++i) r.frame(2, 7, 4106011, 1000, pcm, false, 1000 + i * 20);
    r.service(5000);
    CHECK(!r.ready(nullptr, nullptr, nullptr, 0, nullptr));        // off by default: nothing is kept
    CHECK(r.stats().calls_saved == 0);
    r.set_enabled(true);
    for (int i = 0; i < 30; ++i) r.frame(0, 7, 1, 1, pcm, false, 1000 + i * 20);      // slot 0 = test tone: never recorded
    r.service(9000); CHECK(!r.ready(nullptr, nullptr, nullptr, 0, nullptr) && !r.recording());
    for (int i = 0; i < 30; ++i) r.frame(2, 7, 4106011, 1000, pcm, i % 5 == 4, 1000 + i * 20);   // 24 speech + 6 silent frames
    CHECK(r.recording());
    r.service(1000 + 30 * 20 + 1000); CHECK(!r.ready(nullptr, nullptr, nullptr, 0, nullptr));    // quiet only 1 s: still the same call
    r.service(1000 + 30 * 20 + 3000);
    const int16_t* p = nullptr; size_t n = 0; char name[72]; uint8_t h[kHeaderBytes];
    CHECK(r.ready(&p, &n, name, sizeof(name), h) && n == 4800);
    CHECK(std::strstr(name, "/orcsdr/dmr_") == name && std::strstr(name, "_ts2_4106011.wav") != nullptr);
    CHECK(std::memcmp(h, "RIFF", 4) == 0 && std::memcmp(h + 8, "WAVEfmt ", 8) == 0 && std::memcmp(h + 36, "data", 4) == 0);
    CHECK(h[24] == 0x40 && h[25] == 0x1f && h[40] == (9600 & 255) && h[41] == (9600 >> 8));   // 8000 Hz, 9600 data bytes
    CHECK(p[0] == 1000 && p[4 * 160] == 0 && p[4 * 160 + 159] == 0);                               // silent frame stored as zeros
    for (int i = 0; i < 10; ++i) r.frame(1, 8, 5, 6, pcm, false, 20000 + i * 20);                 // next call arrives while the file is waiting
    r.service(30000); CHECK(r.stats().calls_dropped_busy >= 1);
    r.release(true); CHECK(r.stats().calls_saved == 1 && !r.ready(nullptr, nullptr, nullptr, 0, nullptr));
    for (int i = 0; i < 30; ++i) r.frame(1, 9, 5, 6, pcm, false, 40000 + i * 20);
    r.frame(2, 10, 7, 8, pcm, false, 41000);                                                      // a different call finalizes the previous one
    CHECK(r.ready(nullptr, nullptr, nullptr, 0, nullptr)); r.release(false);
    CHECK(r.stats().calls_saved == 1 && r.stats().calls_discarded >= 1);
    for (int i = 0; i < 5; ++i) r.frame(1, 11, 5, 6, pcm, false, 50000 + i * 20);                 // 100 ms: too short
    r.service(60000); CHECK(!r.ready(nullptr, nullptr, nullptr, 0, nullptr));
    r.set_enabled(false);
    for (int i = 0; i < 30; ++i) r.frame(1, 12, 5, 6, pcm, false, 70000 + i * 20);
    r.service(80000); CHECK(!r.ready(nullptr, nullptr, nullptr, 0, nullptr));
  }
  { // engine -> recorder: only voiced real-call frames, silence frames and the test tone excluded
    dv_voice::Engine e; Recorder r; CHECK(e.begin() && r.begin()); r.set_enabled(true); e.set_enabled(true);
    e.set_speech_sink([](uint8_t slot, uint32_t serial, uint32_t src, uint32_t dst, const int16_t* pcm, bool silent, uint32_t now, void* u) {
      static_cast<Recorder*>(u)->frame(slot, serial, src, dst, pcm, silent, now); }, &r);
    uint32_t now = 1000;
    int16_t tone[160] = {}; CHECK(e.inject_test_pcm(tone, 160, now));
    // The real recorded superframe from terra_153p068 (3 speech frames and 15 silence frames), repeated: 10 groups = 600 ms of speech.
    static const char* kReal = "B9E881526173002A6BB9E881526173002A6BB9E881526173002A6BB9E881526173002A6BB9E881526173002A6BB9E881526173002A6B9BCC8374C8ED298BFF9BCC8374C9FF18AAFF99EE8374ECBD2D8AFFC499A5267CBACD3062EA41C05731F40775808E6CE14331A449ED52B9E881526173002A6BB9E881526173002A6BB9E881526173002A6BB9E881526173002A6BB9E881526173002A6BB9E881526173002A6B";
    auto m = msg(2, 4, true, "B9E881526173002A6B"); m.source_id = 4106008;
    for (int i = 0; i < 162; ++i) { unsigned x; std::sscanf(kReal + 2 * i, "%2x", &x); m.payload[i] = (uint8_t)x; }
    for (int gI = 0; gI < 10; ++gI) { e.handle(m, now + gI * 360); m.new_call = false; }
    r.flush();
    const int16_t* p = nullptr; size_t n = 0; char name[72];
    CHECK(r.ready(&p, &n, name, sizeof(name), nullptr) && n == 180u * 160u);                      // 10 groups x 18 frames, not the tone
    CHECK(std::strstr(name, "_ts2_4106008.wav") != nullptr);
  }
}

// ---- Service: gate, pacing, commands, self test (host double of the firmware sink) -----------------------------------
static std::vector<int16_t> g_sink;
static bool g_exclusive = false, g_allowed = true, g_path = true;
static uint32_t g_svc_now = 5000;
static void sink(const int16_t* p, size_t n) { g_sink.insert(g_sink.end(), p, p + n); }
static void lines(const char* l) { std::printf("  %s\n", l); }
static void service_tests() {
  namespace svc = dv_voice_service;
  svc::Hooks h;
  h.audio_out = sink;
  h.audio_allowed = [] { return g_allowed; };
  h.audio_path_ready = [] { return g_path; };
  h.set_exclusive = [](bool on) { g_exclusive = on; };
  h.now_ms = [] { return g_svc_now; };
  svc::install(h);
  dv_radio::Hooks rh; rh.device_ready = ready; rh.tune = tune; rh.release = release; rh.now_ms = nowf; rh.micros = usf; rh.lost_samples = lostf;
  dv_radio::install(rh);
  CHECK(dv_radio::start());
  CHECK(!svc::running());
  CHECK(svc::start());
  CHECK(svc::running() && g_exclusive);              // the analogue NFM audio is blocked while the service runs
  char rep[400];
  CHECK(svc::selftest(rep, sizeof(rep)) == 0);
  std::printf("  %s\n", rep);
  for (int i = 0; i < 4; ++i) { g_svc_now += 5; svc::pump(); }
  CHECK(g_sink.empty());                              // nothing qualified: silence, never static
  svc::set_buffer_ms(100);                              // the service default is 250 ms; this test paces a 200 ms tone
  CHECK(svc::play_tone(200));
  for (int i = 0; i < 80; ++i) { g_svc_now += 5; svc::pump(); }
  CHECK(g_sink.size() >= 9000 && g_sink.size() <= 9700);   // 200 ms of tone at 48 kHz, played in real time (target buffer 100 ms)
  double sq = 0; for (size_t i = 960; i + 960 < g_sink.size(); ++i) sq += double(g_sink[i]) * g_sink[i];
  CHECK(std::sqrt(sq / (g_sink.size() - 1920)) > 4000);
  g_sink.clear(); g_allowed = false;                  // user sound switch off: audio is withheld, the state says so
  CHECK(svc::play_tone(100));
  for (int i = 0; i < 60; ++i) { g_svc_now += 5; svc::pump(); }
  CHECK(g_sink.empty() && svc::snapshot().engine.phase == dv_voice::Phase::muted);
  g_allowed = true; g_path = false;
  for (int i = 0; i < 4; ++i) { g_svc_now += 5; svc::pump(); }
  CHECK(svc::snapshot().engine.phase == dv_voice::Phase::unavailable);
  g_path = true;
  char l[900];
  svc::format_status(l, sizeof(l));
  CHECK(std::strstr(l, "RTL_DV_AUDIO buffer_ms=") != nullptr && std::strstr(l, " running=1") != nullptr);
  CHECK(svc::handle_command("AUDIO STATUS", false, lines));
  CHECK(svc::handle_command("AUDIO SLOT 2", true, lines));
  g_svc_now += 5; svc::pump();
  CHECK(svc::snapshot().engine.listen == dv_voice::Listen::ts2);
  CHECK(!svc::handle_command("SCAN START", true, lines));
  svc::stop();
  CHECK(!svc::running() && !g_exclusive);             // closing Digital Voice releases the analogue audio path
  dv_radio::stop();
}

int main(int argc, char** argv) {
  gate_tests();
  engine_tests();
  service_tests();
  speech_tests();
  scanner_tests();
  iq_tests();
  // encoder self-consistency (guards the fixture, not the decoder)
  { uint8_t lc[9] = {0, 0, 0x20, 0, 0, 100, 0x3e, 0x99, 0x9b}; std::array<uint8_t, 128> w; encode_lc(lc, w, false); CHECK(row_ok(w)); encode_lc(lc, w, true); CHECK(row_ok(w)); }
  if (argc > 1) corpus_tests(argv[1]);
  std::printf("dv_voice_tests: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
