// Host tool: end-to-end verification harness for JS8 hypotheses on real raw-tone frames.
//
//   js8-e2e-verify --frames FRONT_END.json --checks PARITY.txt [--min-hits N] [--top N]
//   js8-e2e-verify --selftest
//
// Given a candidate parity-check matrix (one check per line: the 0-based variable indices it covers, space separated; '#' comments), the
// tool takes each saved 79-tone frame, extracts the 58 data tones (positions 7-35 and 43-71), and tries EVERY tone-label permutation
// (8! = 40320) in combination with several codeword bit-order layouts, reporting how many parity checks each combination leaves
// unsatisfied. A correct matrix + tone map + layout leaves 0 unsatisfied checks on a clean frame (a noisy frame may leave a few before
// error correction); a wrong combination leaves about half of the checks unsatisfied. It decides nothing by itself: it reports the
// evidence. No expected message text is used anywhere.
//
// --selftest proves the machinery on a synthetic sparse code (random parity-check matrix, random tone labelling and layout) so a pass
// on real data cannot be an artefact of the search.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr size_t kTones = 79, kData = 58, kBits = 174;

struct Frame {
  double hz = 0;
  double sync = 0;
  unsigned hits = 0;
  size_t frame_start = 0;
  int ref = -1;
  std::array<uint8_t, kTones> tones{};
};

using Checks = std::vector<std::vector<uint16_t>>;

const char* layout_name(int layout) {
  static const char* names[] = {"natural/msb-first", "natural/lsb-first", "halves-swapped/msb", "halves-swapped/lsb", "reversed/msb", "reversed/lsb", "reversed+swapped/msb", "reversed+swapped/lsb"};
  return names[layout];
}

// Data-tone indices: 7..35 and 43..71.
std::array<uint8_t, kData> data_tones(const std::array<uint8_t, kTones>& tones) {
  std::array<uint8_t, kData> out{};
  size_t k = 0;
  for (size_t i = 7; i < 36; ++i) out[k++] = tones[i];
  for (size_t i = 43; i < 72; ++i) out[k++] = tones[i];
  return out;
}

// Stream bit i (0..173) after tone-label mapping, in the layout's order, as a code variable index.
inline uint16_t variable_of(int layout, size_t i) {
  const bool reversed = (layout >> 2) & 1 || ((layout >> 2) == 3);
  const bool swapped = ((layout >> 1) & 1) != 0;
  size_t v = i;
  if (layout >= 4) v = kBits - 1 - v;
  (void)reversed;
  if (swapped) v = (v + 87) % kBits;
  return static_cast<uint16_t>(v);
}

// layout bit 0: lsb-first within a tone; bit 1: halves swapped; bit 2: reversed
std::array<uint8_t, kBits> bits_for(const std::array<uint8_t, kData>& tones, const std::array<uint8_t, 8>& labels, int layout) {
  std::array<uint8_t, kBits> code{};
  const bool lsb_first = (layout & 1) != 0;
  const bool swapped = (layout & 2) != 0;
  const bool reversed = (layout & 4) != 0;
  for (size_t s = 0; s < kData; ++s) {
    const uint8_t label = labels[tones[s]];
    for (size_t b = 0; b < 3; ++b) {
      const uint8_t bit = lsb_first ? ((label >> b) & 1u) : ((label >> (2 - b)) & 1u);
      size_t i = 3 * s + b;
      if (reversed) i = kBits - 1 - i;
      if (swapped) i = (i + 87) % kBits;
      code[i] = bit;
    }
  }
  return code;
}

size_t unsatisfied(const Checks& checks, const std::array<uint8_t, kBits>& code) {
  size_t bad = 0;
  for (const auto& check : checks) {
    uint8_t parity = 0;
    for (uint16_t v : check) parity ^= code[v];
    bad += parity;
  }
  return bad;
}

struct Hit {
  size_t weight = 1u << 30;
  std::array<uint8_t, 8> labels{};
  int layout = 0;
};

// Best few (labels, layout) combinations for one frame.
std::vector<Hit> search(const Checks& checks, const std::array<uint8_t, kData>& tones, size_t keep) {
  std::vector<Hit> best;
  std::array<uint8_t, 8> labels{};
  std::iota(labels.begin(), labels.end(), 0);
  do {
    for (int layout = 0; layout < 8; ++layout) {
      const size_t w = unsatisfied(checks, bits_for(tones, labels, layout));
      if (best.size() < keep || w < best.back().weight) {
        Hit h;
        h.weight = w;
        h.labels = labels;
        h.layout = layout;
        best.push_back(h);
        std::sort(best.begin(), best.end(), [](const Hit& a, const Hit& b) { return a.weight < b.weight; });
        if (best.size() > keep) best.pop_back();
      }
    }
  } while (std::next_permutation(labels.begin(), labels.end()));
  return best;
}

bool load_checks(const char* path, Checks* out) {
  std::ifstream file(path);
  if (!file) return false;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::vector<uint16_t> check;
    int v;
    while (in >> v) {
      if (v < 0 || v >= static_cast<int>(kBits)) return false;
      check.push_back(static_cast<uint16_t>(v));
    }
    if (!check.empty()) out->push_back(check);
  }
  return !out->empty();
}

bool load_frames(const char* path, std::vector<Frame>* out) {
  std::ifstream file(path);
  if (!file) return false;
  std::stringstream all;
  all << file.rdbuf();
  const std::string text = all.str();
  const std::regex pattern(
      "\"frame_start_sample\":(\\d+),\"audio_hz\":([0-9.]+),\"sync_score\":([0-9.]+),\"sync_hits\":(\\d+),\"margin\":([0-9.\\-]+),\"ref_index\":(-?\\d+),\"tones\":\"(\\d{79})\"");
  for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
    Frame f;
    f.frame_start = std::strtoull((*it)[1].str().c_str(), nullptr, 10);
    f.hz = std::atof((*it)[2].str().c_str());
    f.sync = std::atof((*it)[3].str().c_str());
    f.hits = static_cast<unsigned>(std::atoi((*it)[4].str().c_str()));
    f.ref = std::atoi((*it)[6].str().c_str());
    const std::string t = (*it)[7].str();
    for (size_t i = 0; i < kTones; ++i) f.tones[i] = static_cast<uint8_t>(t[i] - '0');
    out->push_back(f);
  }
  return !out->empty();
}

// ---- self-test: synthetic sparse code, random labelling and layout; the search must recover them with weight 0.
int selftest() {
  std::mt19937 rng(2026);
  // Build a systematic code: H = [A | I] with a sparse random A (87 x 87), so that codewords are (info, parity = A * info).
  const size_t kInfo = 87;
  std::vector<std::vector<uint8_t>> a(87, std::vector<uint8_t>(kInfo, 0));
  Checks checks;
  for (size_t r = 0; r < 87; ++r) {
    std::vector<uint16_t> check;
    for (size_t c = 0; c < kInfo; ++c)
      if (rng() % 87 < 5) {
        a[r][c] = 1;
        check.push_back(static_cast<uint16_t>(c));
      }
    if (check.empty()) {
      a[r][r] = 1;
      check.push_back(static_cast<uint16_t>(r));
    }
    check.push_back(static_cast<uint16_t>(kInfo + r));   // the parity variable of this check
    checks.push_back(check);
  }
  int failures = 0;
  for (int trial = 0; trial < 4; ++trial) {
    std::array<uint8_t, kBits> code{};
    for (size_t i = 0; i < kInfo; ++i) code[i] = rng() & 1u;
    for (size_t r = 0; r < 87; ++r) {
      uint8_t p = 0;
      for (size_t c = 0; c < kInfo; ++c) p ^= static_cast<uint8_t>(a[r][c] & code[c]);
      code[kInfo + r] = p;
    }
    if (unsatisfied(checks, code) != 0) {
      std::puts("selftest: internal error, constructed word is not a codeword");
      return 1;
    }
    // transmit: pick a random label permutation and layout, build the tone frame
    std::array<uint8_t, 8> labels{};
    std::iota(labels.begin(), labels.end(), 0);
    std::shuffle(labels.begin(), labels.end(), rng);
    const int layout = static_cast<int>(rng() % 8);
    std::array<uint8_t, 8> inverse{};
    for (size_t t = 0; t < 8; ++t) inverse[labels[t]] = static_cast<uint8_t>(t);   // tone t carries label labels[t]
    std::array<uint8_t, kData> tones{};
    const bool lsb_first = (layout & 1) != 0, swapped = (layout & 2) != 0, reversed = (layout & 4) != 0;
    for (size_t s = 0; s < kData; ++s) {
      uint8_t label = 0;
      for (size_t b = 0; b < 3; ++b) {
        size_t i = 3 * s + b;
        if (reversed) i = kBits - 1 - i;
        if (swapped) i = (i + 87) % kBits;
        const uint8_t bit = code[i];
        label = static_cast<uint8_t>(label | (lsb_first ? (bit << b) : (bit << (2 - b))));
      }
      tones[s] = inverse[label];
    }
    const auto found = search(checks, tones, 3);
    // Several (labels, layout) pairs can describe the same stream, so accept any weight-0 hit that reproduces the codeword.
    bool ok = !found.empty() && found[0].weight == 0 && bits_for(tones, found[0].labels, found[0].layout) == code;
    const size_t second = found.size() > 1 ? found[1].weight : 0;
    std::printf("selftest trial %d: layout %d (%s), best weight %zu, runner-up weight %zu -> %s\n", trial, layout, layout_name(layout), found[0].weight, second,
                ok ? "recovered" : "FAILED");
    if (!ok) ++failures;
  }
  std::puts(failures == 0 ? "js8-e2e-verify --selftest: PASS" : "js8-e2e-verify --selftest: FAIL");
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::string frames_path, checks_path;
  unsigned min_hits = 14;
  size_t top = 3;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--selftest") return selftest();
    if (a == "--frames" && i + 1 < argc) frames_path = argv[++i];
    else if (a == "--checks" && i + 1 < argc) checks_path = argv[++i];
    else if (a == "--min-hits" && i + 1 < argc) min_hits = static_cast<unsigned>(std::atoi(argv[++i]));
    else if (a == "--top" && i + 1 < argc) top = static_cast<size_t>(std::atoi(argv[++i]));
    else return 2;
  }
  std::vector<Frame> frames;
  Checks checks;
  if (!load_frames(frames_path.c_str(), &frames) || !load_checks(checks_path.c_str(), &checks)) {
    std::fprintf(stderr, "usage: js8-e2e-verify --frames FRONT_END.json --checks PARITY.txt | --selftest\n");
    return 2;
  }
  std::printf("%zu frames, %zu parity checks\n", frames.size(), checks.size());
  for (const Frame& f : frames) {
    if (f.hits < min_hits) continue;
    const auto best = search(checks, data_tones(f.tones), top);
    std::printf("frame %8zu  %7.1f Hz  sync hits %2u: best unsatisfied checks %zu of %zu (labels", f.frame_start, f.hz, f.hits, best[0].weight, checks.size());
    for (uint8_t l : best[0].labels) std::printf("%u", l);
    std::printf(", layout %s); runner-up %zu\n", layout_name(best[0].layout), best.size() > 1 ? best[1].weight : 0);
  }
  return 0;
}
