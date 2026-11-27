// Stage B host harness: real recorded IQ -> PRODUCTION chain (channelizer, 4FSK frontend, D1 decoder, call manager,
// voice queue) -> dv_voice::Engine (OrcSDR AMBE+2 vocoder, resampler, playout ring) -> WAV files. No Python, no
// reference audio, no reference AMBE vectors are consulted: every WAV comes from the IQ through native code.
// usage: dv_voice_replay <file.cu8> <channel_hz> <outdir> [stem] [jitter_ms]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "dv_radio.hpp"
#include "dv_voice_audio.hpp"

using namespace orcsdr;
static uint32_t g_now = 1000;
static bool ready() { return true; }
static bool tune(uint32_t, uint32_t) { return true; }
static void release() {}
static uint32_t now() { return g_now; }
static uint32_t us() { return g_now * 1000u; }
static uint64_t lost() { return 0; }

struct CallKey {
  uint8_t slot; uint32_t serial;
  bool operator<(const CallKey& o) const { return slot != o.slot ? slot < o.slot : serial < o.serial; }
};
struct CallData { std::vector<int16_t> pcm8; std::vector<std::string> frames; int corrected = 0, total = 0, errors = 0, silence = 0; };
static std::map<CallKey, CallData> g_calls;
static void tap(uint8_t slot, uint32_t serial, const uint8_t wire[9], const int16_t pcm[160], int corrected, int total,
                const char* flags, void*) {
  auto& c = g_calls[{slot, serial}];
  c.pcm8.insert(c.pcm8.end(), pcm, pcm + 160);
  char hex[19];
  for (int i = 0; i < 9; ++i) std::snprintf(hex + 2 * i, 3, "%02X", wire[i]);
  c.frames.emplace_back(hex);
  c.corrected += corrected; c.total += total;
  if (std::strchr(flags, 'E')) ++c.errors;
  if (std::strchr(flags, 'S')) ++c.silence;
}
static void wav(const std::string& path, const std::vector<int16_t>& s, uint32_t rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) std::fputc((v >> (8 * i)) & 255, f); };
  auto w16 = [&](uint16_t v) { std::fputc(v & 255, f); std::fputc(v >> 8, f); };
  const uint32_t bytes = static_cast<uint32_t>(s.size() * 2);
  std::fwrite("RIFF", 1, 4, f); w32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  w32(16); w16(1); w16(1); w32(rate); w32(rate * 2); w16(2); w16(16);
  std::fwrite("data", 1, 4, f); w32(bytes);
  for (int16_t v : s) w16(static_cast<uint16_t>(v));
  std::fclose(f);
}

// Impairments applied to the recorded IQ before it enters the production chain (all optional, key=value after the stem/jitter):
//   offset_hz=N        carrier offset (rotates the IQ)        noise_db=N   add white noise N dB below the measured signal power
//   drift_ppm=N        sample-clock drift (linear resample)    zero_at_s=T zero_ms=D   burst loss (samples zeroed)
//   cut_at_s=T cut_ms=D   splice: samples removed with NO time gap (an undetected discontinuity)
struct Impair {
  double offset_hz = 0, noise_db = 1e9, drift_ppm = 0, zero_at_s = -1, zero_ms = 0, cut_at_s = -1, cut_ms = 0;
  bool any() const { return offset_hz != 0 || noise_db < 1e8 || drift_ppm != 0 || zero_at_s >= 0 || cut_at_s >= 0; }
};
static unsigned long long g_rng = 88172645463325252ull;
static double gauss() {
  auto u = [] { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (g_rng >> 11) * (1.0 / 9007199254740992.0); };
  const double a = u() + 1e-12, b = u();
  return std::sqrt(-2.0 * std::log(a)) * std::cos(6.283185307179586 * b);
}
static void apply_impair(const Impair& im, std::vector<uint8_t>& blk, unsigned long long& index, double sigma, double& phase,
                         double& pos_frac, std::vector<uint8_t>& out) {
  out.clear();
  const size_t pairs = blk.size() / 2;
  for (size_t i = 0; i < pairs; ++i, ++index) {
    const double t = index / 240000.0;
    if (im.cut_at_s >= 0 && t >= im.cut_at_s && t < im.cut_at_s + im.cut_ms / 1000.0) continue;
    double re = blk[2 * i] - 127.5, imag = blk[2 * i + 1] - 127.5;
    if (im.offset_hz != 0) {
      const double c = std::cos(phase), sn = std::sin(phase);
      const double r2 = re * c - imag * sn, i2 = re * sn + imag * c;
      re = r2; imag = i2;
      phase += 6.283185307179586 * im.offset_hz / 240000.0;
    }
    if (im.noise_db < 1e8) { re += sigma * gauss(); imag += sigma * gauss(); }
    if (im.zero_at_s >= 0 && t >= im.zero_at_s && t < im.zero_at_s + im.zero_ms / 1000.0) { re = 0; imag = 0; }
    if (im.drift_ppm != 0) {   // drop (positive ppm) or repeat (negative ppm) one sample every 1e6/|ppm| samples
      pos_frac += std::fabs(im.drift_ppm) * 1e-6;
      if (pos_frac >= 1.0) { pos_frac -= 1.0; if (im.drift_ppm > 0) continue; }
    }
    auto q = [](double v) { v += 127.5; return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : std::lround(v)); };
    out.push_back(q(re)); out.push_back(q(imag));
  }
}

int main(int argc, char** argv) {
  if (argc < 4) { std::puts("usage: dv_voice_replay file.cu8 channel_hz outdir [stem] [jitter_ms]"); return 2; }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) { std::puts("cannot open"); return 2; }
  const std::string out = argv[3];
  std::string stem = argc > 4 ? argv[4] : "capture";
  const uint32_t jitter = argc > 5 ? static_cast<uint32_t>(std::atoi(argv[5])) : 100;
  Impair im;
  bool agc_on = false;
  float gain = 1.0f;
  for (int a = 6; a < argc; ++a) {
    double v = 0; char key[32];
    if (std::sscanf(argv[a], "%31[^=]=%lf", key, &v) != 2) continue;
    if (!std::strcmp(key, "offset_hz")) im.offset_hz = v; else if (!std::strcmp(key, "noise_db")) im.noise_db = v;
    else if (!std::strcmp(key, "drift_ppm")) im.drift_ppm = v; else if (!std::strcmp(key, "zero_at_s")) im.zero_at_s = v;
    else if (!std::strcmp(key, "zero_ms")) im.zero_ms = v; else if (!std::strcmp(key, "cut_at_s")) im.cut_at_s = v;
    else if (!std::strcmp(key, "cut_ms")) im.cut_ms = v;
    else if (!std::strcmp(key, "agc")) agc_on = (v != 0.0);
    else if (!std::strcmp(key, "gain")) gain = static_cast<float>(v);
  }
  double sigma = 0;
  if (im.noise_db < 1e8) {   // measured signal power of the capture (DC removed) fixes the noise level
    long double acc = 0; unsigned long long cnt = 0; std::vector<uint8_t> tmp(1 << 16); size_t k;
    while ((k = std::fread(tmp.data(), 1, tmp.size(), f)) > 0) for (size_t i = 0; i + 1 < k; i += 2) { double a = tmp[i] - 127.5, b = tmp[i + 1] - 127.5; acc += a * a + b * b; ++cnt; }
    std::rewind(f);
    sigma = std::sqrt(static_cast<double>(acc / (cnt ? cnt : 1)) / 2.0 * std::pow(10.0, -im.noise_db / 10.0));
  }
  unsigned long long sample_index = 0; double phase = 0, pos_frac = 0;
  std::vector<uint8_t> shaped;
  dv_radio::Hooks h; h.device_ready = ready; h.tune = tune; h.release = release; h.now_ms = now; h.micros = us; h.lost_samples = lost;
  dv_radio::install(h);
  if (!dv_radio::start()) return 3;
  dv_radio::set_protocol_mask(dv_radio::kMaskDmr);
  dv_radio::set_voice_queue_enabled(true);
  dv_voice::Config cfg; cfg.target_buffer_ms = jitter;
  dv_voice::Engine eng;
  if (!eng.begin(cfg)) return 4;
  eng.set_enabled(true);
  eng.set_agc(agc_on);
  eng.set_gain(gain);
  eng.set_listen(dv_voice::Listen::automatic);
  eng.set_frame_tap(tap, nullptr);
  dv_radio::tune_channel(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)));
  g_now += 700;
  std::vector<uint8_t> buf(16384);
  std::vector<int16_t> out48, pull(4096);
  size_t n;
  unsigned groups = 0, notices = 0;
  auto step = [&](uint32_t dt_ms) {
    dv_radio::VoiceMsg m;
    while (dv_radio::pop_voice(&m)) { if (m.kind == dv_radio::VoiceKind::group) ++groups; else ++notices; eng.handle(m, g_now); }
    size_t due = static_cast<size_t>(dt_ms) * 48;
    while (due) {
      const size_t want = due < pull.size() ? due : pull.size();
      const size_t got = eng.pull(pull.data(), want, g_now);
      out48.insert(out48.end(), pull.begin(), pull.begin() + got);
      if (got < want) break;           // buffering or dry: nothing is played, no catch-up
      due -= got;
    }
  };
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
    std::vector<uint8_t> chunk(buf.begin(), buf.begin() + n);
    if (im.any()) { apply_impair(im, chunk, sample_index, sigma, phase, pos_frac, shaped); chunk = shaped; }
    if (!chunk.empty()) dv_radio::offer_iq(chunk.data(), chunk.size(), 240000);
    const uint32_t dt = static_cast<uint32_t>(n / 2 * 1000 / 240000);
    g_now += dt;
    step(dt);
  }
  for (int i = 0; i < 400; ++i) { g_now += 20; step(20); }   // drain
  const auto vs = dv_radio::voice_status();
  const auto snap = eng.snapshot(g_now);
  std::printf("%s anchors=%u groups_seen=%u qualified=%u protected=%u unqualified=%u calls=%u lost_sf=%u emb_abandon=%u sync_abandon=%u elc_ok=%u elc_bad=%u drops=%u\n",
              stem.c_str(), vs.anchors, vs.groups_seen, vs.qualified, vs.protected_groups, vs.unqualified, vs.calls_started,
              vs.lost_superframes, vs.emb_abandoned, vs.sync_abandoned, vs.embedded_lc_ok, vs.embedded_lc_bad, vs.queue_drops);
  const auto& st = snap.stats;
  std::printf("  engine groups=%llu frames=%llu speech=%llu silence=%llu error=%llu concealed=%llu underruns=%llu overruns=%llu "
              "synth_us last/max/avg=%u/%u/%u latency_ms=%u buffered_peak_ms=%u clipped=%llu\n",
              (unsigned long long)st.groups, (unsigned long long)st.frames, (unsigned long long)st.frames_speech,
              (unsigned long long)st.frames_silence, (unsigned long long)st.frames_error, (unsigned long long)st.frames_concealed,
              (unsigned long long)st.underruns, (unsigned long long)st.overruns, st.synth_us_last, st.synth_us_max, st.synth_us_avg,
              st.latency_ms_last, st.buffered_peak_ms, (unsigned long long)st.clipped_samples);
  for (auto& kv : g_calls) {
    const std::string base = out + "/" + stem + "_ts" + std::to_string(kv.first.slot) + "_call" + std::to_string(kv.first.serial);
    wav(base + ".wav", kv.second.pcm8, 8000);
    FILE* t = std::fopen((base + ".frames.txt").c_str(), "w");
    if (t) { for (auto& hx : kv.second.frames) std::fprintf(t, "%s\n", hx.c_str()); std::fclose(t); }
    std::printf("  call ts%u #%u frames=%zu corrected=%d total=%d err=%d silence=%d\n", kv.first.slot, kv.first.serial,
                kv.second.frames.size(), kv.second.corrected, kv.second.total, kv.second.errors, kv.second.silence);
  }
  wav(out + "/" + stem + "_speaker48k.wav", out48, 48000);
  std::printf("  speaker 48k samples=%zu (%.2f s) groups_handled=%u notices=%u\n", out48.size(), out48.size() / 48000.0, groups, notices);
  eng.end();
  dv_radio::stop();
  return 0;
}
