// Host tool: runs the JS8 Normal receive front end (the same backend the Tab5 runs) over a long 12 kHz WAV with a sliding 15 s
// window and reports the sync candidates and raw 79-tone frames it finds, optionally near reference audio frequencies.
//
//   js8-wav-front <wav> [--step SECONDS] [--ref HZ]... [--tol HZ] [--json]
//
// Evidence only: no FEC, CRC or text. A frame is located by its absolute sample offset in the WAV (window start + candidate start).
// Nothing is normalised or gain-controlled, and no expected answer is injected: --ref only marks which candidates fall near a
// frequency the reference decoder reported, so hits, misses and extras can be compared afterwards.
#include "js8_native_backend.hpp"

#include "ft8_wav_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <wav> [--step SECONDS] [--ref HZ]... [--tol HZ] [--json]\n", argv[0]);
    return 2;
  }
  double step_s = 2.0, tol = 7.0;
  float min_score = 0.10f;
  bool json = false, show_candidates = false, show_log = false;
  std::vector<double> refs;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--step" && i + 1 < argc) step_s = std::atof(argv[++i]);
    else if (a == "--ref" && i + 1 < argc) refs.push_back(std::atof(argv[++i]));
    else if (a == "--tol" && i + 1 < argc) tol = std::atof(argv[++i]);
    else if (a == "--json") json = true;
    else if (a == "--min-score" && i + 1 < argc) min_score = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--candidates") show_candidates = true;
    else if (a == "--log") show_log = true;
    else return 2;
  }
  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(argv[1], &wav) || wav.sample_rate != 12000u) {
    std::fprintf(stderr, "need a 12000 Hz mono 16-bit WAV\n");
    return 2;
  }
  orcsdr::js8::native::Backend backend;
  orcsdr::js8::native::Config config;
  config.candidate_limit = 32;
  config.min_score = min_score;
  if (!backend.begin(12000, orcsdr::js8::Submode::normal, config)) return 1;
  const size_t window = 180000;
  const size_t step = static_cast<size_t>(step_s * 12000.0);
  struct Hit {
    size_t window_start;
    size_t frame_start;
    double hz;
    float sync;
    unsigned hits;
    float margin;
    int ref;   // index of the matching reference frequency or -1
    std::array<uint8_t, orcsdr::js8::kChannelSymbols> tones;
  };
  std::vector<Hit> hits;
  size_t windows = 0;
  orcsdr::ft8::Decode none[16];
  size_t message_count = 0;
  for (size_t start = 0; start + window <= wav.samples.size(); start += step) {
    backend.begin_slot(0);
    backend.offer_audio(wav.samples.data() + start, window);
    const size_t n_messages = backend.finish_slot(none, 16);
    ++windows;
    message_count += n_messages;
    if (show_log)
      for (size_t i = 0; i < backend.raw_count(); ++i) {
        const auto* r = backend.raw(i);
        if (!r->attempted) continue;
        const unsigned long long abs_start = static_cast<unsigned long long>(start) + r->start_sample;
        std::printf("frame window=%.1fs start=%llu hz=%.2f dt_ms=%d sync_hits=%u sync=%.3f margin=%.3f initial_syndrome=%u bp_iter=%u osd_order=%u corrections=%u final_syndrome=%u crc=%s ",
                    static_cast<double>(start) / 12000.0, abs_start, static_cast<double>(r->audio_hz), static_cast<int>(r->dt_ms), r->sync_hits,
                    static_cast<double>(r->sync_score), static_cast<double>(r->mean_margin), r->initial_syndrome, r->bp_iterations, r->osd_order,
                    r->hard_corrections, r->final_syndrome, r->crc_valid ? "ok" : "fail");
        if (r->crc_valid) std::printf("kind=%u payload=%s ", r->frame_kind, r->payload);
        if (r->rendered) std::printf("text=\"%s\" ", r->text);
        std::printf("backend_ms=%u\n", backend.stats().total_ms);
      }
    if (show_candidates)
      for (size_t i = 0; i < backend.candidate_count(); ++i) {
        const auto* c = backend.candidate(i);
        const double hz = backend.first_hz() + c->base_bin * 6.25;
        for (size_t k = 0; k < refs.size(); ++k)
          if (std::fabs(hz - refs[k]) <= tol)
            std::printf("candidate ref %.1f: window %.1f s hz %.1f start_row %u sync %.3f\n", refs[k], static_cast<double>(start) / 12000.0, hz,
                        static_cast<unsigned>(c->start_row), static_cast<double>(c->score));
      }
    for (size_t i = 0; i < backend.raw_count(); ++i) {
      const auto* r = backend.raw(i);
      Hit h{};
      h.window_start = start;
      h.frame_start = start + static_cast<size_t>(std::max<int64_t>(0, std::llround((r->dt_ms / 1000.0 + 0.5) * 12000.0)));
      h.hz = r->audio_hz;
      h.sync = r->sync_score;
      h.hits = r->sync_hits;
      h.margin = r->mean_margin;
      h.ref = -1;
      for (size_t k = 0; k < refs.size(); ++k)
        if (std::fabs(h.hz - refs[k]) <= tol) h.ref = static_cast<int>(k);
      h.tones = r->frame.tones;
      hits.push_back(h);
    }
  }
  if (json) {
    std::printf("{\"wav\":\"%s\",\"windows\":%zu,\"step_s\":%.2f,\"raw_frames\":[", argv[1], windows, step_s);
    for (size_t i = 0; i < hits.size(); ++i) {
      const Hit& h = hits[i];
      std::printf("%s{\"window_start_sample\":%zu,\"frame_start_sample\":%zu,\"audio_hz\":%.2f,\"sync_score\":%.3f,\"sync_hits\":%u,\"margin\":%.3f,\"ref_index\":%d,\"tones\":\"",
                  i ? "," : "", h.window_start, h.frame_start, h.hz, static_cast<double>(h.sync), h.hits, static_cast<double>(h.margin), h.ref);
      for (uint8_t t : h.tones) std::printf("%u", t);
      std::printf("\"}");
    }
    std::printf("]}\n");
    return 0;
  }
  std::printf("windows=%zu raw_frames=%zu\n", windows, hits.size());
  for (size_t k = 0; k < refs.size(); ++k) {
    const Hit* best = nullptr;
    size_t count = 0;
    for (const Hit& h : hits)
      if (h.ref == static_cast<int>(k)) {
        ++count;
        if (best == nullptr || h.sync > best->sync) best = &h;
      }
    if (best != nullptr)
      std::printf("ref %7.1f Hz: %zu windows saw a frame within %.0f Hz; best sync %.3f (hits %u, margin %.2f) at %.1f Hz, frame start sample %zu (%.2f s)\n",
                  refs[k], count, tol, static_cast<double>(best->sync), best->hits, static_cast<double>(best->margin), best->hz, best->frame_start,
                  static_cast<double>(best->frame_start) / 12000.0);
    else
      std::printf("ref %7.1f Hz: no raw frame within %.0f Hz in any window\n", refs[k], tol);
  }
  size_t extras = 0;
  for (const Hit& h : hits) extras += h.ref < 0 ? 1u : 0u;
  std::printf("raw frames not near any reference frequency: %zu\n", extras);
  return 0;
}
