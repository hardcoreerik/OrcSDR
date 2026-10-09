#include "js8-tone-map.hpp"
#include "../apps/orcsdr-tab5/ui/js8_frame.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

bool parse_frame(const std::string& line, orcsdr::js8::DataTones* out) {
  if (out == nullptr || line.size() != orcsdr::js8::kChannelSymbols) return false;

  orcsdr::js8::RawFrame frame{};
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] < '0' || line[i] > '7') return false;
    frame.tones[i] = static_cast<uint8_t>(line[i] - '0');
  }
  return orcsdr::js8::extract_data_tones(frame, out);
}

void print_bits(const orcsdr::js8::reconstruct::Bits174& bits) {
  for (size_t i = 0; i < orcsdr::js8::reconstruct::kBits; ++i)
    std::putchar(bits.get(i) ? '1' : '0');
  std::putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(
        stderr,
        "usage: %s <79-tone-frames.txt> <labels:0,1,2,3,4,5,6,7>\n",
        argv[0]);
    return 2;
  }

  orcsdr::js8::reconstruct::ToneLabels labels{};
  if (!orcsdr::js8::reconstruct::parse_labels(argv[2], &labels)) {
    std::fprintf(stderr, "invalid labels\n");
    return 2;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::perror(argv[1]);
    return 2;
  }

  std::string line;
  size_t line_no = 0;
  size_t emitted = 0;
  std::printf("# JS8_CODEWORDS labels=%s source=%s\n", argv[2], argv[1]);

  while (std::getline(input, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    orcsdr::js8::DataTones data{};
    if (!parse_frame(line, &data)) {
      std::fprintf(stderr,
                   "invalid verified Normal frame at line %zu\n", line_no);
      return 2;
    }

    orcsdr::js8::reconstruct::Bits174 bits{};
    if (!orcsdr::js8::reconstruct::tones_to_bits(data.tones, labels, &bits))
      return 1;

    print_bits(bits);
    ++emitted;
  }

  std::printf("# JS8_CODEWORDS_EMITTED=%zu\n", emitted);
  return emitted != 0 ? 0 : 1;
}
