// Host tool: classifies, for every message the reference decoder found in a recording, the stage at which the PRODUCTION native
// backend (the code that runs on the Tab5) lost it. Build with -DORCSDR_FT8_DIAG; production firmware is built without it.
//
//   ft8-native-diagnose <12k-mono-i16.wav> <ft8|ft4> --ref FILE [--k N] [--gate N] [--min-score X] [--lead-ms N] [--json]
//
//   --ref FILE   reference decodes, one per line: <audio_hz> <dt_s> <message...>   ('#' starts a comment); see tools/ft8-reference/
//   --k / --gate coarse candidates refined / refined candidates sent through the FEC gates (default 64 / 32, the firmware values)
//   --lead-ms N  prepends N ms of silence (an experiment only: a frame that starts before the file begins is still cut off)
//
// Stages, in order: window coverage -> coarse sync -> refinement/dedupe -> ranking (gate limit) -> soft demod/LDPC -> CRC -> message
// unpacking -> plausibility. A CRC-valid message of an unsupported type is reported as such, never as an RF failure. The tool
// orchestrates only what the backend recorded; it adds no decoding and injects no expected answer.
#include "ft8_native_backend.hpp"

#include "ft8_wav_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Reference {
  double hz = 0;
  double dt = 0;
  std::string text;
};

std::string normalize(const std::string& in) {
  std::string out;
  bool space = false;
  for (char c : in) {
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

std::string json_escape(const std::string& in) {
  std::string out;
  for (unsigned char c : in) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 32) { char escaped[7]; std::snprintf(escaped, sizeof(escaped), "\\u%04x", c); out += escaped; }
    else out += static_cast<char>(c);
  }
  return out;
}

const char* outcome_name(int outcome) {
  switch (outcome) {
    case 0: return "soft demod failed";
    case 1: return "LDPC did not converge";
    case 2: return "LDPC converged but CRC failed";
    case 3: return "CRC valid, message type not unpacked";
    case 4: return "CRC valid, unpacked but not plausible";
    case 5: return "accepted";
    default: return "not attempted";
  }
}

std::string payload_type(const std::array<uint8_t, 91>& bits, bool ft4) {
  orcsdr::ft8::codec::PayloadBits payload{};
  std::copy_n(bits.begin(), payload.size(), payload.begin());
  if (ft4) orcsdr::ft8::codec::restore_ft4_payload(&payload);
  const int i3 = payload[74] * 4 + payload[75] * 2 + payload[76];
  const int n3 = payload[71] * 4 + payload[72] * 2 + payload[73];
  char text[40];
  if (i3 == 0) std::snprintf(text, sizeof(text), "i3=0 n3=%d", n3);
  else std::snprintf(text, sizeof(text), "i3=%d", i3);
  return text;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <wav> <ft8|ft4> --ref FILE [--k N] [--gate N] [--min-score X] [--lead-ms N] [--json]\n", argv[0]);
    return 2;
  }
  if (std::strcmp(argv[2], "ft8") != 0 && std::strcmp(argv[2], "ft4") != 0) return 2;
  const bool ft4 = std::strcmp(argv[2], "ft4") == 0;
  std::string ref_path;
  int k = 64, gate = 32, lead_ms = 0;
  float min_score = 0.10f;
  bool json = false;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ref" && i + 1 < argc) ref_path = argv[++i];
    else if (a == "--k" && i + 1 < argc) k = std::atoi(argv[++i]);
    else if (a == "--gate" && i + 1 < argc) gate = std::atoi(argv[++i]);
    else if (a == "--min-score" && i + 1 < argc) min_score = static_cast<float>(std::atof(argv[++i]));
    else if (a == "--lead-ms" && i + 1 < argc) lead_ms = std::atoi(argv[++i]);
    else if (a == "--json") json = true;
    else return 2;
  }
  if (ref_path.empty() || k < 1 || k > 64 || gate < 1 || gate > 64 || lead_ms < 0 || lead_ms > 15000 || !std::isfinite(min_score) || min_score < 0 || min_score > 1) return 2;
  std::vector<Reference> refs;
  {
    std::ifstream file(ref_path);
    if (!file) { std::fprintf(stderr, "cannot open reference file\n"); return 2; }
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream in(line);
      Reference r;
      if (!(in >> r.hz >> r.dt) || !std::isfinite(r.hz) || !std::isfinite(r.dt)) return 2;
      std::getline(in, r.text);
      r.text = normalize(r.text);
      if (!r.text.empty()) refs.push_back(r);
    }
  }
  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(argv[1], &wav)) {
    std::fprintf(stderr, "need a 12000 Hz mono 16-bit WAV\n");
    return 2;
  }
  std::vector<int16_t> samples(static_cast<size_t>(lead_ms) * 12u, 0);
  samples.insert(samples.end(), wav.samples.begin(), wav.samples.end());

  using orcsdr::ftx::Mode;
  orcsdr::ftx::native::Backend backend;
  orcsdr::ftx::native::Config config;
  config.candidate_k = static_cast<uint16_t>(k);
  config.gate = static_cast<uint16_t>(gate);
  config.min_score = min_score;
  if (!backend.begin(12000, ft4 ? Mode::ft4 : Mode::ft8, config)) return 1;
  backend.begin_slot(1791440160000ull);
  backend.offer_audio(samples.data(), samples.size());
  orcsdr::ft8::Decode out[64];
  const size_t n = backend.finish_slot(out, 64);
  const auto& d = backend.diag();
  const auto& profile = orcsdr::ftx::profile(ft4 ? Mode::ft4 : Mode::ft8);
  const double spacing_hz = profile.tone_spacing_millihz / 1000.0;
  const double symbol_s = static_cast<double>(profile.symbol_samples) / 12000.0;
  const double frame_s = symbol_s * profile.channel_symbols;
  const double buffered_s = static_cast<double>(d.buffered_samples) / 12000.0;
  const double lead_s = lead_ms / 1000.0;
  const double tol_hz = 0.75 * spacing_hz;
  const double tol_s = ft4 ? 0.12 : 0.15;
  const size_t row_step = d.fine_rows_per_symbol / 2;

  auto fine_hz = [&](const orcsdr::ftx::native::DiagRefined& r) { return (2.0 * d.first_bin + r.base_bin) * d.bin_hz / 2.0; };
  auto fine_dt = [&](const orcsdr::ftx::native::DiagRefined& r) { return r.start_row * d.fine_hop_samples / 12000.0 - 0.5 - lead_s; };
  auto coarse_hz = [&](const orcsdr::ftx::native::DiagCoarse& c) { return (d.first_bin + c.base_bin) * d.bin_hz; };
  auto coarse_dt = [&](const orcsdr::ftx::native::DiagCoarse& c) { return c.start_row * row_step * d.fine_hop_samples / 12000.0 - 0.5 - lead_s; };

  size_t decoded = 0;
  if (json) std::printf("{\"mode\":\"%s\",\"decodes\":%zu,\"coarse\":%zu,\"refined\":%zu,\"references\":[", ft4 ? "ft4" : "ft8", n, d.coarse_count, d.refined_count);
  else std::printf("%s: %zu references, %zu decoded; %zu coarse candidates, %zu refined, gate %d, k %d, buffered %.2f s\n", ft4 ? "FT4" : "FT8", refs.size(), n,
                   d.coarse_count, d.refined_count, gate, k, buffered_s);
  bool first = true;
  for (const Reference& ref : refs) {
    char verdict[320];
    const char* stage = "?";
    bool ok = false;
    for (size_t i = 0; i < n; ++i)
      if (normalize(out[i].message) == ref.text) ok = true;
    const double frame_start = 0.5 + ref.dt;   // seconds from the start of the recording to the first tone
    int overlap = 0;
    for (const Reference& other : refs)
      if (&other != &ref && std::fabs(other.hz - ref.hz) <= 10.0 && std::fabs(other.dt - ref.dt) <= 0.6) ++overlap;

    // nearest coarse and refined candidates
    const orcsdr::ftx::native::DiagCoarse* near_coarse = nullptr;
    double near_coarse_gap = 1e9;
    for (size_t i = 0; i < d.coarse_count; ++i) {
      const double gf = std::fabs(coarse_hz(d.coarse[i]) - ref.hz), gt = std::fabs(coarse_dt(d.coarse[i]) - ref.dt);
      if (gf <= 1.5 * spacing_hz && gt <= 2.0 * tol_s && gf + gt * 20.0 < near_coarse_gap) {
        near_coarse = &d.coarse[i];
        near_coarse_gap = gf + gt * 20.0;
      }
    }
    int refined_rank = -1;
    const orcsdr::ftx::native::DiagRefined* near_refined = nullptr;
    for (size_t i = 0; i < d.refined_count; ++i) {
      const auto& r = d.refined[i];
      if (std::fabs(fine_hz(r) - ref.hz) <= tol_hz && std::fabs(fine_dt(r) - ref.dt) <= tol_s) {
        refined_rank = static_cast<int>(i);
        near_refined = &r;
        break;
      }
    }

    if (ok) {
      stage = "decoded";
      ++decoded;
      std::snprintf(verdict, sizeof(verdict), "decoded%s", refined_rank >= 0 ? "" : " (candidate not matched by position)");
    } else if (frame_start < -0.001) {
      stage = "window";
      std::snprintf(verdict, sizeof(verdict), "the frame starts %.2f s BEFORE the recording/slot buffer (first %d symbols cut off); the search only considers frames that fit inside the buffer",
                    -frame_start, static_cast<int>(std::ceil(-frame_start / symbol_s)));
    } else if (frame_start + lead_s + frame_s > buffered_s + 0.001) {
      stage = "window";
      std::snprintf(verdict, sizeof(verdict), "the frame ends %.2f s after the buffer", frame_start + lead_s + frame_s - buffered_s);
    } else if (near_coarse == nullptr) {
      stage = "coarse_sync";
      std::snprintf(verdict, sizeof(verdict), "no coarse sync candidate within %.1f Hz / %.2f s", 1.5 * spacing_hz, 2.0 * tol_s);
    } else if (near_refined == nullptr) {
      stage = "refinement";
      std::snprintf(verdict, sizeof(verdict), "coarse candidate (score %.2f) was merged away or moved by refinement", static_cast<double>(near_coarse->score));
    } else if (near_refined->outcome < 0) {
      stage = "ranking";
      std::snprintf(verdict, sizeof(verdict), "refined candidate (score %.2f) ranked #%d, beyond the gate limit of %d", static_cast<double>(near_refined->score), refined_rank + 1, gate);
    } else {
      const int o = near_refined->outcome;
      stage = o == 0 ? "demod" : o == 1 ? "ldpc" : o == 2 ? "crc" : o == 3 ? "unpack" : o == 4 ? "plausibility" : "other";
      char extra[96] = "";
      if (o == 3 || o == 4) std::snprintf(extra, sizeof(extra), " (%s)", payload_type(near_refined->message, ft4).c_str());
      if (o == 1) std::snprintf(extra, sizeof(extra), " (%d iterations)", near_refined->ldpc_iterations);
      std::snprintf(verdict, sizeof(verdict), "refined candidate #%d score %.2f: %s%s", refined_rank + 1, static_cast<double>(near_refined->score), outcome_name(o), extra);
    }
    if (json) {
      std::printf("%s{\"hz\":%.0f,\"dt\":%.1f,\"message\":\"%s\",\"stage\":\"%s\",\"overlap\":%d,\"detail\":\"%s\"}", first ? "" : ",", ref.hz, ref.dt, json_escape(ref.text).c_str(), stage, overlap, json_escape(verdict).c_str());
    } else {
      std::printf("%-8s %6.0f Hz dt %+.1f %-26s %s%s\n", stage, ref.hz, ref.dt, ref.text.c_str(), verdict, overlap ? "  [overlaps another reference]" : "");
    }
    first = false;
  }
  if (json) std::printf("],\"reference_decoded\":%zu}\n", decoded);
  else {
    std::printf("-> %zu of %zu reference messages decoded\n", decoded, refs.size());
    size_t extras = 0;
    for (size_t i = 0; i < n; ++i) {
      bool known = false;
      for (const Reference& ref : refs)
        if (normalize(out[i].message) == ref.text) known = true;
      if (!known) {
        ++extras;
        std::printf("extra decode not in the reference list: %s\n", out[i].message);
      }
    }
    if (extras == 0) std::printf("no decodes outside the reference list\n");
  }
  return 0;
}
