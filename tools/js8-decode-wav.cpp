// Host tool: runs the JS8 Normal soft-decision decoder (js8_decoder: LLR -> BP -> bounded OSD, parity + CRC-12 acceptance) at given
// (start sample, tone-0 frequency) hypotheses in a 12 kHz mono WAV and logs, per frame, every stage of the decode.
//
//   js8-decode-wav <wav> --at START_SAMPLE BASE_HZ [--label NAME]... [--order N] [--no-osd] [--gain G]
//
// The hypotheses come from the command line (a reference list or the front end's candidates); nothing about the expected message is
// supplied, so the printed text is whatever the decoder verified. Output is one line per hypothesis (key=value, greppable).
#include "ft8_wav_common.hpp"
#include "js8_decoder.hpp"
#include "js8_demod.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  struct At { size_t start; double hz; std::string label; };
  std::vector<At> list;
  orcsdr::js8::decoder::Config config;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--at" && i + 2 < argc) {
      At at{static_cast<size_t>(std::atoll(argv[i + 1])), std::atof(argv[i + 2]), ""};
      list.push_back(at);
      i += 2;
    } else if (a == "--label" && i + 1 < argc && !list.empty()) list.back().label = argv[++i];
    else if (a == "--order" && i + 1 < argc) config.osd_order = static_cast<uint8_t>(std::atoi(argv[++i]));
    else if (a == "--no-osd") config.use_osd = false;
    else if (a == "--gain" && i + 1 < argc) config.llr_gain = static_cast<float>(std::atof(argv[++i]));
    else return 2;
  }
  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(argv[1], &wav) || wav.sample_rate != 12000u) {
    std::fprintf(stderr, "need a 12000 Hz mono 16-bit WAV\n");
    return 2;
  }
  auto workspace = std::make_unique<orcsdr::js8::decoder::Workspace>();
  static float energy[orcsdr::js8::kChannelSymbols][8];
  int rendered_count = 0;
  for (const At& at : list) {
    orcsdr::js8::DemodStats stats{};
    const auto t0 = std::chrono::steady_clock::now();
    if (!orcsdr::js8::demodulate_energies(wav.samples.data(), wav.samples.size(), orcsdr::js8::Submode::normal, at.start, static_cast<float>(at.hz), energy, &stats)) {
      std::printf("label=%s start=%zu hz=%.2f status=out_of_range\n", at.label.c_str(), at.start, at.hz);
      continue;
    }
    orcsdr::js8::decoder::Result r;
    const bool ok = orcsdr::js8::decoder::decode(energy, config, workspace.get(), &r);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("label=%s start=%zu hz=%.2f sync_hits=%u sync_score=%.3f margin=%.3f initial_syndrome=%u ", at.label.c_str(), at.start, at.hz, stats.sync_hits,
                static_cast<double>(stats.sync_score), static_cast<double>(stats.mean_margin), r.initial_syndrome);
    if (ok) {
      static const char* names[] = {"none", "hard", "bp", "osd"};
      std::printf("status=%s method=%s bp_iter=%u osd_order=%u osd_tested=%u corrections=%u final_syndrome=%u crc=ok discrepancy=%.3f kind=%u flags=%u payload=%s ",
                  r.rendered ? "message" : "valid_unrendered", names[static_cast<int>(r.method)], r.bp_iterations, r.osd_order, r.osd_tested, r.hard_corrections,
                  r.final_syndrome, static_cast<double>(r.osd_discrepancy), r.fields.kind, r.fields.flags, r.fields.text);
      if (r.rendered) { std::printf("text=\"%s\" ", r.message.text); ++rendered_count; }
    } else {
      std::printf("status=no_decode bp_iter=%u osd_tested=%u crc=fail ", r.bp_iterations, r.osd_tested);
    }
    std::printf("ms=%.1f\n", ms);
  }
  std::printf("summary hypotheses=%zu messages=%d\n", list.size(), rendered_count);
  return 0;
}
