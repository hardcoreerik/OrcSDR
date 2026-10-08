// Host-only experiment: candidate-local time/frequency refinement before soft demodulation (Task 3, experiment 2).
//
//   ft8-wav-refine <12k-mono-i16.wav> <ft8|ft4> <coarse_rows> <coarse_bins> [options]
//     --ref FILE      reference decodes (<audio_hz> <dt_s> <message...>) to score against
//     --topk N        coarse candidates to refine / attempt (default 32)
//     --min-score X   coarse sync threshold (default 0.10)
//     --gate N        attempt only the N best candidates after refinement, ranked by refined sync score (default: all)
//     --metric M      soft metric: linear_symbol (default), amplitude_symbol, linear_frame, amplitude_frame
//     --gain X        LLR gain (default 1)
//     --oracle        also try each reference signal at its WSJT-X position (refined locally): separates search misses from
//                     demodulation/FEC limits
//     --no-refine     attempt the coarse candidates directly (same top-K, same gates): the control
//
// Stage 1 is the production coarse search (spectral grid + sync::search). Stage 2 takes the top-K coarse candidates and,
// for each, runs a short coordinate search over sample-accurate start time and sub-bin frequency, scoring ONLY the
// protocol-defined synchronization symbols with exact single-frequency correlation (the same energy definition as the
// spectral oracle). It then builds the candidate-local energy grid (one row per channel symbol, one bin per tone) at the
// refined position and hands it to the unchanged pipeline::try_candidate (soft demod -> LDPC -> CRC -> unpack ->
// plausibility). This is an exact-correlation prototype to measure the coverage gain; it is not the P4 implementation and
// its wall times are host numbers only. It prints no SNR.

#include "ft8_mode.hpp"
#include "ft8_pipeline.hpp"
#include "ft8_spectral.hpp"
#include "ft8_wav_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

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

bool read_reference_texts(const char* path, std::set<std::string>* out) {
  std::ifstream file(path);
  if (!file) return false;
  std::string line;
  while (std::getline(file, line)) {
    const std::size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    std::istringstream in(line);
    double hz = 0, dt = 0;
    if (!(in >> hz >> dt)) continue;
    std::string rest;
    std::getline(in, rest);
    rest = normalize(rest);
    if (!rest.empty()) out->insert(rest);
  }
  return !out->empty();
}

constexpr double kPi = 3.14159265358979323846;

// |sum x[n] e^{-j w n}|^2 / n^2 over one symbol window: the spectral oracle's definition.
double tone_energy(const int16_t* x, std::size_t n, double freq_hz, double rate_hz) {
  const double w = 2.0 * kPi * freq_hz / rate_hz;
  const double cw = std::cos(w), sw = std::sin(w);
  double c = 1.0, s = 0.0, re = 0.0, im = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double v = x[i];
    re += v * c;
    im -= v * s;
    const double nc = c * cw - s * sw;
    s = s * cw + c * sw;
    c = nc;
  }
  return (re * re + im * im) / (static_cast<double>(n) * static_cast<double>(n));
}

struct Refiner {
  const orcsdr::ftx::ModeProfile& p;
  const int16_t* x;
  std::size_t count;
  double spacing_hz;
  int rounds = 1;
  bool joint = false;

  bool in_range(long start) const {
    return start >= 0 &&
           static_cast<std::size_t>(start) + static_cast<std::size_t>(p.channel_symbols) * p.symbol_samples <= count;
  }

  // Sync contrast at an exact (start sample, base frequency): (expected - competing) / (expected + competing).
  double sync_score(long start, double base_hz) const {
    double expected = 0.0, total = 0.0;
    for (std::size_t b = 0; b < p.sync_block_count; ++b) {
      const auto& block = p.sync[b];
      for (std::size_t k = 0; k < block.length; ++k) {
        const std::size_t sym = block.first_symbol + k;
        const int16_t* w = x + start + sym * p.symbol_samples;
        for (uint8_t tone = 0; tone < p.tone_count; ++tone) {
          const double e = tone_energy(w, p.symbol_samples, base_hz + tone * spacing_hz, p.sample_rate_hz);
          total += e;
          if (tone == block.tones[k]) expected += e;
        }
      }
    }
    const double competing = total - expected;
    return (expected - competing) / (expected + competing + 1.0e-12);
  }

  // Coordinate search around a coarse position. Returns the refined (start, base_hz) and its sync score.
  double refine(long start0, double hz0, double time_span, double freq_span, long* start, double* hz) const {
    long best_t = start0;
    double best_f = hz0;
    double best = in_range(best_t) ? sync_score(best_t, best_f) : -2.0;
    auto scan_time = [&](double span, int steps) {
      const long base = best_t;
      for (int i = -steps; i <= steps; ++i) {
        const long t = base + std::lround(span * i / steps);
        if (t == base || !in_range(t)) continue;
        const double s = sync_score(t, best_f);
        if (s > best) { best = s; best_t = t; }
      }
    };
    auto scan_freq = [&](double span, int steps) {
      const double base = best_f;
      for (int i = -steps; i <= steps; ++i) {
        const double f = base + span * i / steps;
        if (i == 0 || f < 100.0) continue;
        if (!in_range(best_t)) continue;
        const double s = sync_score(best_t, f);
        if (s > best) { best = s; best_f = f; }
      }
    };
    if (joint) {
      const long bt = best_t;
      const double bf = best_f;
      for (int ti = -4; ti <= 4; ++ti)
        for (int fi = -4; fi <= 4; ++fi) {
          const long t = bt + std::lround(time_span * ti / 4.0);
          const double f = bf + freq_span * fi / 4.0;
          if ((ti == 0 && fi == 0) || !in_range(t) || f < 100.0) continue;
          const double sc = sync_score(t, f);
          if (sc > best) { best = sc; best_t = t; best_f = f; }
        }
    }
    for (int r = 0; r < (joint ? 0 : rounds); ++r) {
      scan_time(time_span, 4);                  // coarse time, +-1 coarse hop in quarter-hop steps
      scan_freq(freq_span, 4);                  // coarse frequency, +-1 coarse bin in quarter-bin steps
    }
    scan_time(time_span / 4.0, 2);              // fine time
    scan_freq(freq_span / 4.0, 2);              // fine frequency
    *start = best_t;
    *hz = best_f;
    return best;
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: %s <wav> <ft8|ft4> <coarse_rows> <coarse_bins> [--ref FILE] [--topk N] [--min-score X] [--no-refine]\n", argv[0]);
    return 2;
  }
  const char* wav_path = argv[1];
  orcsdr::ftx::Mode mode;
  if (std::strcmp(argv[2], "ft8") == 0) mode = orcsdr::ftx::Mode::ft8;
  else if (std::strcmp(argv[2], "ft4") == 0) mode = orcsdr::ftx::Mode::ft4;
  else return 2;
  uint8_t crow = 0, cbin = 0;
  if (!ft8_wav_tools::parse_u8(argv[3], &crow) || !ft8_wav_tools::parse_u8(argv[4], &cbin) || crow < 1 || cbin < 1) return 2;
  const char* ref_path = nullptr;
  std::size_t topk = 32;
  float min_score = 0.10f;
  bool refine_on = true;
  std::size_t gate = 0;
  bool oracle = false;
  bool joint = false;
  int rounds = 1;
  double watch_hz = -1.0;
  orcsdr::ftx::demod::Config dcfg{};
  uint8_t iters = 20;
  float norm = 0.80f;
  for (int i = 5; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ref" && i + 1 < argc) ref_path = argv[++i];
    else if (a == "--topk" && i + 1 < argc) topk = std::strtoul(argv[++i], nullptr, 10);
    else if (a == "--min-score" && i + 1 < argc) min_score = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--gate" && i + 1 < argc) gate = std::strtoul(argv[++i], nullptr, 10);
    else if (a == "--metric" && i + 1 < argc) {
      const std::string m = argv[++i];
      using M = orcsdr::ftx::demod::Metric;
      if (m == "linear_symbol") dcfg.metric = M::linear_symbol;
      else if (m == "amplitude_symbol") dcfg.metric = M::amplitude_symbol;
      else if (m == "linear_frame") dcfg.metric = M::linear_frame;
      else if (m == "amplitude_frame") dcfg.metric = M::amplitude_frame;
      else if (m == "lse_amplitude") dcfg.metric = M::lse_amplitude;
      else return 2;
    }
    else if (a == "--gain" && i + 1 < argc) dcfg.llr_gain = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--iters" && i + 1 < argc) iters = static_cast<uint8_t>(std::atoi(argv[++i]));
    else if (a == "--norm" && i + 1 < argc) norm = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--oracle") oracle = true;
    else if (a == "--joint") joint = true;
    else if (a == "--rounds" && i + 1 < argc) rounds = std::atoi(argv[++i]);
    else if (a == "--watch" && i + 1 < argc) watch_hz = std::atof(argv[++i]);
    else if (a == "--no-refine") refine_on = false;
    else return 2;
  }
  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(wav_path, &wav)) { std::fprintf(stderr, "unsupported WAV\n"); return 2; }
  std::set<std::string> reference;
  if (ref_path != nullptr && !read_reference_texts(ref_path, &reference)) { std::fprintf(stderr, "bad reference\n"); return 2; }

  const auto& p = orcsdr::ftx::profile(mode);
  const std::size_t hop = p.symbol_samples / crow;
  const std::size_t rows = 1 + (wav.samples.size() - p.symbol_samples) / hop;
  constexpr uint32_t kFirstMillihz = 200000, kLastMillihz = 3000000;
  const uint32_t spacing = p.tone_spacing_millihz / cbin;
  const std::size_t bins = 1 + (kLastMillihz - kFirstMillihz) / spacing;

  const auto t_all = Clock::now();
  std::vector<float> energy(rows * bins, 0.0f);
  orcsdr::ftx::spectral::Config sc{};
  sc.first_bin_millihz = kFirstMillihz;
  sc.bin_spacing_millihz = spacing;
  sc.bin_count = static_cast<uint16_t>(bins);
  sc.rows_per_symbol = crow;
  orcsdr::ftx::spectral::ReferenceAccumulator spectral{};
  if (!orcsdr::ftx::spectral::begin(&spectral, p, sc, orcsdr::ftx::spectral::OutputGrid{energy.data(), rows, bins}) ||
      !orcsdr::ftx::spectral::offer(&spectral, wav.samples.data(), wav.samples.size())) {
    std::fprintf(stderr, "spectral analysis failed\n");
    return 1;
  }
  const double spectral_ms = ms_since(t_all);
  orcsdr::ftx::sync::EnergyGrid grid{energy.data(), orcsdr::ftx::spectral::rows_written(spectral), bins, bins};
  orcsdr::ftx::sync::Geometry geometry{crow, cbin};

  orcsdr::ftx::sync::SearchConfig search{};
  search.min_score = min_score;
  search.suppress_time_rows = crow;
  search.suppress_frequency_bins = cbin;
  std::vector<orcsdr::ftx::sync::Candidate> ranked(topk);
  const auto t_search = Clock::now();
  const std::size_t n = orcsdr::ftx::sync::search(p, grid, geometry, search, ranked.data(), ranked.size());
  const double search_ms = ms_since(t_search);

  orcsdr::ftx::pipeline::Config pipe{};
  pipe.search = search;
  pipe.demod = dcfg;
  pipe.ldpc.max_iterations = iters;
  pipe.ldpc.normalization = norm;
  orcsdr::ftx::pipeline::Workspace ws{};
  const double step_hz = p.tone_spacing_millihz / 1000.0;
  Refiner refiner{p, wav.samples.data(), wav.samples.size(), step_hz, rounds, joint};

  std::set<std::string> accepted;
  std::size_t attempts = 0, ldpc_conv = 0, crc_pass = 0, skipped_dup = 0, skipped_range = 0;
  std::vector<std::pair<long, double>> tried;  // refined positions already attempted
  const auto t_ref = Clock::now();
  double refine_ms = 0.0, gates_ms = 0.0;
  std::vector<float> local(static_cast<std::size_t>(p.channel_symbols) * p.tone_count);

  struct Refined { long start; double hz; double score; float coarse; };
  std::vector<Refined> refined;
  for (std::size_t i = 0; i < n; ++i) {
    const auto& c = ranked[i];
    long start = static_cast<long>(c.start_row) * static_cast<long>(hop);
    double hz = (kFirstMillihz + static_cast<double>(c.base_bin) * spacing) / 1000.0;
    double score = c.score;
    if (refine_on) {
      const auto t0 = Clock::now();
      long rs = start;
      double rh = hz;
      score = refiner.refine(start, hz, static_cast<double>(hop), spacing / 1000.0, &rs, &rh);
      refine_ms += ms_since(t0);
      start = rs;
      hz = rh;
    }
    if (watch_hz > 0 && std::fabs(hz - watch_hz) < 12.0)
      std::printf("WATCH coarse_rank=%zu coarse_sync=%.3f coarse_start_s=%.3f coarse_hz=%.2f -> refined start_s=%.3f hz=%.2f score=%.3f\n", i + 1,
                  c.score, static_cast<double>(c.start_row) * hop / p.sample_rate_hz,
                  (kFirstMillihz + static_cast<double>(c.base_bin) * spacing) / 1000.0, start / 12000.0, hz, score);
    refined.push_back({start, hz, score, c.score});
  }
  if (refine_on)
    std::stable_sort(refined.begin(), refined.end(), [](const Refined& a, const Refined& b) { return a.score > b.score; });
  std::size_t gated = 0;
  for (const Refined& rc : refined) {
    if (gate != 0 && gated >= gate) break;
    const long start = rc.start;
    const double hz = rc.hz;
    const auto& c = rc;
    if (!refiner.in_range(start)) { ++skipped_range; continue; }
    bool dup = false;
    for (const auto& q : tried)
      if (std::labs(q.first - start) < static_cast<long>(p.symbol_samples / 16) && std::fabs(q.second - hz) < step_hz / 8.0) { dup = true; break; }
    if (dup) { ++skipped_dup; continue; }
    tried.emplace_back(start, hz);
    ++gated;

    const auto tg = Clock::now();
    for (std::size_t s = 0; s < p.channel_symbols; ++s)
      for (uint8_t tone = 0; tone < p.tone_count; ++tone)
        local[s * p.tone_count + tone] = static_cast<float>(
            tone_energy(wav.samples.data() + start + s * p.symbol_samples, p.symbol_samples, hz + tone * step_hz, p.sample_rate_hz));
    orcsdr::ftx::sync::EnergyGrid lg{local.data(), p.channel_symbols, p.tone_count, p.tone_count};
    orcsdr::ftx::sync::Candidate lc{};
    lc.score = c.coarse;
    orcsdr::ftx::pipeline::FrameResult frame{};
    orcsdr::ftx::pipeline::CandidateTrace trace{};
    const auto outcome = orcsdr::ftx::pipeline::try_candidate(p, lg, orcsdr::ftx::sync::Geometry{1, 1}, lc, pipe, &ws, &frame, &trace);
    gates_ms += ms_since(tg);
    ++attempts;
    if (trace.ldpc_converged) ++ldpc_conv;
    if (trace.crc_ok) ++crc_pass;
    if (outcome == orcsdr::ftx::pipeline::Outcome::accepted) accepted.insert(normalize(frame.standard.text));
  }
  const double total_ms = ms_since(t_all);
  (void)t_ref;

  std::size_t false_accepts = 0, decoded_ref = 0;
  for (const auto& t : accepted)
    if (!reference.empty() && !reference.count(t)) ++false_accepts;
  if (reference.empty()) false_accepts = accepted.size();  // no reference given: any accept is unverified (noise corpus)
  for (const auto& t : reference)
    if (accepted.count(t)) ++decoded_ref;

  std::printf("REFINE mode=%s file=%s coarse=%u,%u topk=%zu refine=%s\n", p.name, wav_path, crow, cbin, topk, refine_on ? "on" : "off");
  std::printf("TIMING_MS spectral=%.1f coarse_search=%.1f refine=%.1f local_grid+gates=%.1f total=%.1f\n", spectral_ms, search_ms, refine_ms,
              gates_ms, total_ms);
  std::printf("COUNTS coarse_candidates=%zu attempted=%zu skipped_duplicate=%zu skipped_out_of_range=%zu ldpc_converged=%zu crc_pass=%zu\n", n,
              attempts, skipped_dup, skipped_range, ldpc_conv, crc_pass);
  std::printf("RESULT accepted=%zu reference=%zu decoded_reference=%zu false_accepts=%zu\n", accepted.size(), reference.size(), decoded_ref,
              false_accepts);
  for (const auto& t : accepted)
    std::printf("ACCEPTED %s%s\n", t.c_str(), reference.empty() ? "" : (reference.count(t) ? "  [in reference]" : "  [NOT IN REFERENCE]"));

  if (oracle && ref_path != nullptr) {
    std::ifstream rf(ref_path);
    std::string line;
    std::printf("ORACLE (each reference signal at its WSJT-X position, refined locally, then the unchanged gates)\n");
    while (std::getline(rf, line)) {
      const std::size_t hash = line.find('#');
      if (hash != std::string::npos) line.resize(hash);
      std::istringstream in(line);
      double rhz = 0, rdt = 0;
      if (!(in >> rhz >> rdt)) continue;
      std::string rest;
      std::getline(in, rest);
      rest = normalize(rest);
      long st = std::lround((rdt + 0.5) * p.sample_rate_hz);
      double hz2 = rhz;
      if (!refiner.in_range(st)) { std::printf("  %-26s %7.1f Hz dt %+.1f  OUT_OF_RECORDING\n", rest.c_str(), rhz, rdt); continue; }
      long rs = st;
      const double sc = refiner.refine(st, rhz, static_cast<double>(hop), spacing / 1000.0, &rs, &hz2);
      std::vector<float> lg2(static_cast<std::size_t>(p.channel_symbols) * p.tone_count);
      for (std::size_t sy = 0; sy < p.channel_symbols; ++sy)
        for (uint8_t tone = 0; tone < p.tone_count; ++tone)
          lg2[sy * p.tone_count + tone] = static_cast<float>(
              tone_energy(wav.samples.data() + rs + sy * p.symbol_samples, p.symbol_samples, hz2 + tone * step_hz, p.sample_rate_hz));
      orcsdr::ftx::sync::EnergyGrid g2{lg2.data(), p.channel_symbols, p.tone_count, p.tone_count};
      orcsdr::ftx::sync::Candidate c2{};
      orcsdr::ftx::pipeline::FrameResult fr{};
      orcsdr::ftx::pipeline::CandidateTrace tr{};
      const auto oc = orcsdr::ftx::pipeline::try_candidate(p, g2, orcsdr::ftx::sync::Geometry{1, 1}, c2, pipe, &ws, &fr, &tr);
      std::printf("  %-26s %7.1f Hz dt %+.1f  refined_sync=%.3f moved %+ld samples %+.2f Hz contrast=%.2f ldpc_iter=%u -> %s\n", rest.c_str(),
                  rhz, rdt, sc, rs - st, hz2 - rhz, tr.mean_symbol_contrast, static_cast<unsigned>(tr.ldpc_iterations),
                  orcsdr::ftx::pipeline::outcome_name(oc));
    }
  }
  return 0;
}
