// Host-only diagnosis of the native FT8/FT4 receive chain on a real recording, against an external reference list.
//
//   ft8-wav-diagnose <12k-mono-i16.wav> <ft8|ft4> <rows_per_symbol> <bins_per_tone> [options]
//     --ref FILE       reference decodes, one per line: <audio_hz> <dt_s> <message...>   ('#' starts a comment)
//     --cap N          candidates the pipeline may attempt (default 16, the production limit)
//     --min-score X    sync score threshold (default 0.10)
//     --all N          candidates kept for diagnosis (default 1024); every one is attempted so the log shows what a
//                      larger cap would have recovered
//     --list           print every ranked candidate with its outcome
//
// Every reference signal is classified by where it was lost, so a miss is never one undifferentiated bucket. This tool
// orchestrates the same module functions the production path uses (sync::search, pipeline::try_candidate); it adds no
// decoding of its own. It prints no SNR: there is no calibrated SNR estimator yet.

#include "ft8_mode.hpp"
#include "ft8_pipeline.hpp"
#include "ft8_spectral.hpp"
#include "ft8_wav_common.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Reference {
  double audio_hz = 0;
  double dt_s = 0;
  std::string message;
};

std::string normalize(const std::string& text) {
  std::string out;
  bool space = false;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  return out;
}

bool read_references(const char* path, std::vector<Reference>* out) {
  std::ifstream file(path);
  if (!file) return false;
  std::string line;
  while (std::getline(file, line)) {
    const std::size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    std::istringstream in(line);
    Reference r;
    if (!(in >> r.audio_hz >> r.dt_s)) continue;
    std::string rest;
    std::getline(in, rest);
    r.message = normalize(rest);
    if (!r.message.empty()) out->push_back(r);
  }
  return !out->empty();
}

struct Options {
  const char* wav = nullptr;
  orcsdr::ftx::Mode mode = orcsdr::ftx::Mode::ft8;
  uint8_t rows = 2;
  uint8_t bins = 1;
  const char* ref = nullptr;
  std::size_t cap = 16;
  float min_score = 0.10f;
  std::size_t all = 1024;
  bool list = false;
};

bool parse_args(int argc, char** argv, Options* o) {
  if (argc < 5) return false;
  o->wav = argv[1];
  if (std::strcmp(argv[2], "ft8") == 0) o->mode = orcsdr::ftx::Mode::ft8;
  else if (std::strcmp(argv[2], "ft4") == 0) o->mode = orcsdr::ftx::Mode::ft4;
  else return false;
  if (!ft8_wav_tools::parse_u8(argv[3], &o->rows) || !ft8_wav_tools::parse_u8(argv[4], &o->bins)) return false;
  for (int i = 5; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&](const char** value) { if (i + 1 >= argc) return false; *value = argv[++i]; return true; };
    const char* v = nullptr;
    if (a == "--ref") { if (!next(&o->ref)) return false; }
    else if (a == "--cap") { if (!next(&v)) return false; o->cap = std::strtoul(v, nullptr, 10); }
    else if (a == "--all") { if (!next(&v)) return false; o->all = std::strtoul(v, nullptr, 10); }
    else if (a == "--min-score") { if (!next(&v)) return false; o->min_score = static_cast<float>(std::atof(v)); }
    else if (a == "--list") o->list = true;
    else return false;
  }
  return o->rows >= 1 && o->bins >= 1 && o->cap >= 1 && o->all >= o->cap;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse_args(argc, argv, &opt)) {
    std::fprintf(stderr,
                 "usage: %s <wav> <ft8|ft4> <rows_per_symbol> <bins_per_tone> [--ref FILE] [--cap N] [--min-score X] "
                 "[--all N] [--list]\n",
                 argv[0]);
    return 2;
  }
  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(opt.wav, &wav)) {
    std::fprintf(stderr, "unsupported WAV: require RIFF PCM, mono, 12000 Hz, 16-bit\n");
    return 2;
  }
  std::vector<Reference> refs;
  if (opt.ref != nullptr && !read_references(opt.ref, &refs)) {
    std::fprintf(stderr, "cannot read reference file %s\n", opt.ref);
    return 2;
  }

  const auto& p = orcsdr::ftx::profile(opt.mode);
  const std::size_t hop = p.symbol_samples / opt.rows;
  const std::size_t rows = 1 + (wav.samples.size() - p.symbol_samples) / hop;
  constexpr uint32_t kFirstMillihz = 200000, kLastMillihz = 3000000;
  const uint32_t spacing = p.tone_spacing_millihz / opt.bins;
  const std::size_t bins = 1 + (kLastMillihz - kFirstMillihz) / spacing;

  // ---- spectral grid
  std::vector<float> energy(rows * bins, 0.0f);
  orcsdr::ftx::spectral::Config sc{};
  sc.first_bin_millihz = kFirstMillihz;
  sc.bin_spacing_millihz = spacing;
  sc.bin_count = static_cast<uint16_t>(bins);
  sc.rows_per_symbol = opt.rows;
  orcsdr::ftx::spectral::ReferenceAccumulator spectral{};
  const auto t_spectral = Clock::now();
  if (!orcsdr::ftx::spectral::begin(&spectral, p, sc, orcsdr::ftx::spectral::OutputGrid{energy.data(), rows, bins}) ||
      !orcsdr::ftx::spectral::offer(&spectral, wav.samples.data(), wav.samples.size())) {
    std::fprintf(stderr, "spectral analysis failed\n");
    return 1;
  }
  const double spectral_ms = ms_since(t_spectral);
  orcsdr::ftx::sync::EnergyGrid grid{energy.data(), orcsdr::ftx::spectral::rows_written(spectral), bins, bins};
  orcsdr::ftx::sync::Geometry geometry{opt.rows, opt.bins};

  // ---- sync: every position above threshold (before suppression), then the suppressed ranked list
  const std::size_t frame_rows = static_cast<std::size_t>(p.channel_symbols - 1) * opt.rows + 1;
  const std::size_t tone_bins = static_cast<std::size_t>(p.tone_count - 1) * opt.bins + 1;
  std::size_t pre_nms = 0, scored = 0;
  const auto t_scan = Clock::now();
  for (std::size_t r = 0; r + frame_rows <= grid.rows; ++r)
    for (std::size_t b = 0; b + tone_bins <= grid.bins; ++b) {
      orcsdr::ftx::sync::Candidate c{};
      if (!orcsdr::ftx::sync::score_candidate(p, grid, geometry, static_cast<uint16_t>(r), static_cast<uint16_t>(b), &c))
        continue;
      ++scored;
      if (c.score >= opt.min_score) ++pre_nms;
    }
  const double scan_ms = ms_since(t_scan);

  orcsdr::ftx::sync::SearchConfig search{};
  search.min_score = opt.min_score;
  search.suppress_time_rows = opt.rows;
  search.suppress_frequency_bins = opt.bins;
  std::vector<orcsdr::ftx::sync::Candidate> ranked(opt.all);
  const auto t_search = Clock::now();
  const std::size_t n_ranked = orcsdr::ftx::sync::search(p, grid, geometry, search, ranked.data(), ranked.size());
  const double search_ms = ms_since(t_search);
  ranked.resize(n_ranked);

  // ---- every ranked candidate through the gates
  orcsdr::ftx::pipeline::Config pipe{};
  pipe.search = search;
  orcsdr::ftx::pipeline::Workspace workspace{};
  std::vector<orcsdr::ftx::pipeline::Outcome> outcome(n_ranked);
  std::vector<orcsdr::ftx::pipeline::FrameResult> frame(n_ranked);
  std::vector<orcsdr::ftx::pipeline::CandidateTrace> trace(n_ranked);
  std::size_t demod_ok = 0, ldpc_attempts = 0, ldpc_converged = 0, crc_pass = 0, unpack_ok = 0, plausible = 0;
  const auto t_gates = Clock::now();
  for (std::size_t i = 0; i < n_ranked; ++i) {
    outcome[i] = orcsdr::ftx::pipeline::try_candidate(p, grid, geometry, ranked[i], pipe, &workspace, &frame[i], &trace[i]);
    using O = orcsdr::ftx::pipeline::Outcome;
    if (outcome[i] != O::demod_failed) { ++demod_ok; ++ldpc_attempts; }
    if (trace[i].ldpc_converged) ++ldpc_converged;
    if (trace[i].crc_ok) ++crc_pass;
    if (outcome[i] == O::not_plausible || outcome[i] == O::accepted) ++unpack_ok;
    if (outcome[i] == O::accepted) ++plausible;
  }
  const double gates_ms = ms_since(t_gates);

  using O = orcsdr::ftx::pipeline::Outcome;
  auto audio_hz = [&](uint16_t bin) { return (kFirstMillihz + static_cast<double>(bin) * spacing) / 1000.0; };
  auto start_s = [&](uint16_t row) { return static_cast<double>(row) * hop / p.sample_rate_hz; };

  // ---- accepted messages within the cap (what production would have shown) and beyond it
  std::set<std::string> accepted_in_cap, accepted_all;
  std::size_t duplicate_accepts = 0;
  for (std::size_t i = 0; i < n_ranked; ++i) {
    if (outcome[i] != O::accepted) continue;
    const std::string text = normalize(frame[i].standard.text);
    if (i < opt.cap && !accepted_in_cap.insert(text).second) ++duplicate_accepts;
    accepted_all.insert(text);
  }
  std::set<std::string> reference_texts;
  for (const auto& r : refs) reference_texts.insert(r.message);
  std::size_t false_accepts = 0, decoded_refs = 0, would_decode = 0;
  for (const auto& t : accepted_in_cap)
    if (!refs.empty() && !reference_texts.count(t)) ++false_accepts;
  for (const auto& t : reference_texts) {
    if (accepted_in_cap.count(t)) ++decoded_refs;
    if (accepted_all.count(t)) ++would_decode;
  }

  std::printf("DIAG mode=%s file=%s samples=%zu rows=%zu bins=%zu rows_per_symbol=%u bins_per_tone=%u cap=%zu "
              "min_score=%.2f\n",
              p.name, opt.wav, wav.samples.size(), grid.rows, grid.bins, opt.rows, opt.bins, opt.cap, opt.min_score);
  std::printf("TIMING_MS spectral=%.1f sync_scan(all positions)=%.1f sync_search(ranked)=%.1f gates(all %zu cand)=%.1f\n",
              spectral_ms, scan_ms, search_ms, n_ranked, gates_ms);
  std::printf("CANDIDATES positions_scored=%zu above_threshold_before_nms=%zu after_nms=%zu attempted_in_cap=%zu "
              "attempted_total=%zu\n",
              scored, pre_nms, n_ranked, std::min(opt.cap, n_ranked), n_ranked);
  std::printf("GATES(all candidates) demod_ok=%zu ldpc_attempts=%zu ldpc_converged=%zu crc_pass=%zu unpack_ok=%zu "
              "plausible=%zu\n",
              demod_ok, ldpc_attempts, ldpc_converged, crc_pass, unpack_ok, plausible);
  {
    std::size_t in_cap_attempts = 0, in_cap_conv = 0, in_cap_crc = 0, in_cap_plausible = 0;
    for (std::size_t i = 0; i < std::min(opt.cap, n_ranked); ++i) {
      if (outcome[i] != O::demod_failed) ++in_cap_attempts;
      if (trace[i].ldpc_converged) ++in_cap_conv;
      if (trace[i].crc_ok) ++in_cap_crc;
      if (outcome[i] == O::accepted) ++in_cap_plausible;
    }
    std::printf("GATES(within cap) ldpc_attempts=%zu ldpc_converged=%zu crc_pass=%zu plausible=%zu\n", in_cap_attempts,
                in_cap_conv, in_cap_crc, in_cap_plausible);
  }
  std::printf("RESULT accepted_in_cap=%zu unique_messages=%zu duplicate_accepts=%zu accepted_if_cap_unlimited=%zu "
              "reference=%zu decoded_reference=%zu would_decode_reference_if_unlimited=%zu false_accepts=%zu\n",
              static_cast<std::size_t>(std::count(outcome.begin(), outcome.begin() + std::min(opt.cap, n_ranked), O::accepted)),
              accepted_in_cap.size(), duplicate_accepts, accepted_all.size(), reference_texts.size(), decoded_refs,
              would_decode, false_accepts);
  for (const auto& t : accepted_in_cap)
    std::printf("ACCEPTED %s%s\n", t.c_str(), refs.empty() ? "" : (reference_texts.count(t) ? "  [in reference]" : "  [NOT IN REFERENCE]"));
  for (const auto& t : accepted_all)
    if (!accepted_in_cap.count(t))
      std::printf("ACCEPTED_BEYOND_CAP %s%s\n", t.c_str(),
                  refs.empty() ? "" : (reference_texts.count(t) ? "  [in reference]" : "  [NOT IN REFERENCE]"));

  // ---- classify every reference signal
  if (!refs.empty()) {
    const double tol_f = 0.75 * p.tone_spacing_millihz / 1000.0;
    const double tol_t = opt.mode == orcsdr::ftx::Mode::ft8 ? 0.25 : 0.15;
    std::map<std::string, int> by_category;
    const double recording_s = static_cast<double>(wav.samples.size()) / p.sample_rate_hz;
    const double frame_s = static_cast<double>(p.channel_symbols) * p.symbol_samples / p.sample_rate_hz;
    const double tone_span_hz = (p.tone_count - 1) * p.tone_spacing_millihz / 1000.0;
    std::printf("REFERENCE_CLASSIFICATION (tolerance %.1f Hz, %.2f s; WSJT-X start = dt + 0.5 s; analysis band %.0f-%.0f Hz)\n",
                tol_f, tol_t, kFirstMillihz / 1000.0, kLastMillihz / 1000.0);

    // One candidate is credited to at most one reference: closest pairs first, so a neighbouring signal's decode is not
    // attributed to a weaker signal a few Hz away.
    struct Pair { std::size_t ref, cand; double distance; };
    std::vector<Pair> pairs;
    for (std::size_t ri = 0; ri < refs.size(); ++ri)
      for (std::size_t i = 0; i < n_ranked; ++i) {
        const double df = std::fabs(audio_hz(ranked[i].base_bin) - refs[ri].audio_hz);
        const double dt = std::fabs(start_s(ranked[i].start_row) - (refs[ri].dt_s + 0.5));
        if (df <= tol_f && dt <= tol_t) pairs.push_back({ri, i, df / tol_f + dt / tol_t});
      }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.distance < y.distance; });
    std::vector<int> assigned(refs.size(), -1);
    std::set<std::size_t> used;
    for (const Pair& pr : pairs)
      if (assigned[pr.ref] < 0 && !used.count(pr.cand)) { assigned[pr.ref] = static_cast<int>(pr.cand); used.insert(pr.cand); }

    std::size_t in_scope = 0;
    for (std::size_t ri = 0; ri < refs.size(); ++ri) {
      const Reference& r = refs[ri];
      const double ref_start = r.dt_s + 0.5;
      const int best = assigned[ri];
      // The best sync score anywhere near the truth position, even if it was below threshold or suppressed.
      double neighborhood_best = -1; std::size_t nr = 0, nb = 0;
      const long centre_row = static_cast<long>(std::lround(ref_start * p.sample_rate_hz / hop));
      const long centre_bin = static_cast<long>(std::lround((r.audio_hz * 1000.0 - kFirstMillihz) / spacing));
      for (long rr = centre_row - 3; rr <= centre_row + 3; ++rr)
        for (long bb = centre_bin - 2; bb <= centre_bin + 2; ++bb) {
          if (rr < 0 || bb < 0) continue;
          orcsdr::ftx::sync::Candidate c{};
          if (!orcsdr::ftx::sync::score_candidate(p, grid, geometry, static_cast<uint16_t>(rr), static_cast<uint16_t>(bb), &c))
            continue;
          if (c.score > neighborhood_best) { neighborhood_best = c.score; nr = static_cast<std::size_t>(rr); nb = static_cast<std::size_t>(bb); }
        }
      std::string category;
      char detail[170];
      if (best >= 0) {
        const std::size_t rank = static_cast<std::size_t>(best);
        const bool decoded_text = accepted_in_cap.count(r.message) != 0;
        if (rank >= opt.cap) category = std::string("RANK_BEYOND_CAP/") + orcsdr::ftx::pipeline::outcome_name(outcome[rank]);
        else if (outcome[rank] == O::accepted) category = decoded_text ? "DECODED" : "ACCEPTED_WRONG_MESSAGE";
        else category = std::string("CANDIDATE_") + orcsdr::ftx::pipeline::outcome_name(outcome[rank]);
        std::snprintf(detail, sizeof(detail), "rank=%zu sync=%.3f contrast=%.2f ldpc_iter=%u", rank + 1, ranked[rank].score,
                      trace[rank].mean_symbol_contrast, static_cast<unsigned>(trace[rank].ldpc_iterations));
      } else if (r.audio_hz * 1000.0 < kFirstMillihz || (r.audio_hz + tone_span_hz) * 1000.0 > kLastMillihz) {
        category = "OUT_OF_ANALYSIS_BAND";
        std::snprintf(detail, sizeof(detail), "occupies %.0f-%.0f Hz", r.audio_hz, r.audio_hz + tone_span_hz);
      } else if (ref_start < 0.0) {
        category = "STARTS_BEFORE_RECORDING";
        std::snprintf(detail, sizeof(detail), "starts at %.2f s", ref_start);
      } else if (ref_start + frame_s > recording_s) {
        category = "TRUNCATED_BY_END";
        std::snprintf(detail, sizeof(detail), "ends at %.2f s, recording is %.2f s", ref_start + frame_s, recording_s);
      } else if (neighborhood_best >= opt.min_score) {
        category = "SUPPRESSED_BY_NMS";
        std::snprintf(detail, sizeof(detail), "best sync near truth %.3f at row %zu bin %zu (above threshold, no surviving candidate)",
                      neighborhood_best, nr, nb);
      } else {
        category = "NO_SYNC_CANDIDATE";
        std::snprintf(detail, sizeof(detail), "best sync near truth %.3f (threshold %.2f)", neighborhood_best, opt.min_score);
      }
      if (category != "OUT_OF_ANALYSIS_BAND" && category != "STARTS_BEFORE_RECORDING" && category != "TRUNCATED_BY_END") ++in_scope;
      ++by_category[category];
      std::printf("  %-34s %7.1f Hz dt %+.1f  %-26s %s\n", category.c_str(), r.audio_hz, r.dt_s, r.message.c_str(), detail);
    }
    std::printf("CLASSIFICATION_SUMMARY  in_scope=%zu_of_%zu", in_scope, refs.size());
    for (const auto& kv : by_category) std::printf("  %s=%d", kv.first.c_str(), kv.second);
    std::printf("\n");
  }

  if (opt.list) {
    for (std::size_t i = 0; i < n_ranked; ++i)
      std::printf("CAND rank=%zu%s start_s=%.3f audio_hz=%.2f sync=%.4f outcome=%s%s%s\n", i + 1, i < opt.cap ? "" : "(beyond cap)",
                  start_s(ranked[i].start_row), audio_hz(ranked[i].base_bin), ranked[i].score,
                  orcsdr::ftx::pipeline::outcome_name(outcome[i]), outcome[i] == O::accepted ? " text=" : "",
                  outcome[i] == O::accepted ? frame[i].standard.text : "");
  }
  return 0;
}
