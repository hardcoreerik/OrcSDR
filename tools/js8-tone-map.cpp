#include "js8-tone-map.hpp"
#include "../apps/orcsdr-tab5/ui/js8_frame.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
using orcsdr::js8::reconstruct::ToneData;
using orcsdr::js8::reconstruct::ToneLabels;

struct Result {
  size_t rank = orcsdr::js8::reconstruct::kBits + 1;
  ToneLabels labels{};
};

bool parse_frame(const std::string& line, ToneData* out) {
  if (out == nullptr || line.size() != orcsdr::js8::kChannelSymbols) return false;
  orcsdr::js8::RawFrame frame{};
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] < '0' || line[i] > '7') return false;
    frame.tones[i] = static_cast<uint8_t>(line[i] - '0');
  }
  orcsdr::js8::DataTones data{};
  if (!orcsdr::js8::extract_data_tones(frame, &data)) return false;
  *out = data.tones;
  return true;
}

void print_labels(const ToneLabels& labels) {
  for (size_t i = 0; i < labels.size(); ++i)
    std::printf("%s%u", i ? "," : "", static_cast<unsigned>(labels[i]));
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <normal-79-tone-frames.txt>\n", argv[0]);
    return 2;
  }
  std::ifstream input(argv[1]);
  if (!input) {
    std::perror(argv[1]);
    return 2;
  }
  std::vector<ToneData> frames;
  std::string line;
  size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    ToneData data{};
    if (!parse_frame(line, &data)) {
      std::fprintf(stderr, "invalid verified-Normal 79-tone frame at line %zu\n", line_no);
      return 2;
    }
    frames.push_back(data);
  }
  if (frames.empty()) {
    std::fprintf(stderr, "no frames\n");
    return 2;
  }

  std::array<Result, 12> best{};
  ToneLabels labels{{0,1,2,3,4,5,6,7}};
  size_t tested = 0;
  do {
    const size_t cutoff = best.back().rank <= orcsdr::js8::reconstruct::kBits
                              ? best.back().rank
                              : orcsdr::js8::reconstruct::kBits;
    const size_t rank = orcsdr::js8::reconstruct::mapped_rank(frames.data(), frames.size(), labels, cutoff);
    Result candidate{rank, labels};
    if (candidate.rank < best.back().rank) {
      best.back() = candidate;
      std::sort(best.begin(), best.end(), [](const Result& a, const Result& b) { return a.rank < b.rank; });
    }
    ++tested;
  } while (std::next_permutation(labels.begin(), labels.end()));

  std::printf("JS8_TONE_MAP frames=%zu permutations=%zu\n", frames.size(), tested);
  if (frames.size() <= 87)
    std::printf("WARNING frames<=87: rank alone cannot establish an 87-dimensional code; collect more independent frames.\n");
  for (const Result& r : best) {
    if (r.rank > orcsdr::js8::reconstruct::kBits) continue;
    std::printf("rank=%zu labels=", r.rank);
    print_labels(r.labels);
    std::printf("\n");
  }
  return 0;
}
