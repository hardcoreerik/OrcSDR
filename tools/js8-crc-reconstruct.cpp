#include "js8-crc-reconstruct.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

using orcsdr::js8::reconstruct::Info87;

struct Match {
  const char* direction = "";
  bool reverse_crc_bits = false;
  uint16_t poly = 0;
  uint16_t offset = 0;
};

uint16_t raw_crc(const Info87& word, bool lsb, uint16_t poly) {
  return lsb ? orcsdr::js8::reconstruct::crc12_lsb(word, poly)
             : orcsdr::js8::reconstruct::crc12_msb(word, poly);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <87-bit-info-words.txt>\n", argv[0]);
    return 2;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::perror(argv[1]);
    return 2;
  }

  std::vector<Info87> words;
  std::string line;
  size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    Info87 word{};
    if (!orcsdr::js8::reconstruct::parse_info87(
            line.c_str(), line.size(), &word)) {
      std::fprintf(stderr, "invalid 87-bit word at line %zu\n", line_no);
      return 2;
    }
    words.push_back(word);
  }

  if (words.size() < 2) {
    std::fprintf(stderr, "need at least two distinct information words\n");
    return 2;
  }

  std::vector<Match> matches;
  for (int lsb = 0; lsb <= 1; ++lsb) {
    for (int reverse_crc = 0; reverse_crc <= 1; ++reverse_crc) {
      for (uint16_t poly = 1; poly < 0x1000u; ++poly) {
        const uint16_t first_raw = raw_crc(words[0], lsb != 0, poly);
        const uint16_t first_observed =
            orcsdr::js8::reconstruct::observed_crc(
                words[0], reverse_crc != 0);
        const uint16_t offset =
            static_cast<uint16_t>(first_raw ^ first_observed);

        bool valid = true;
        for (size_t i = 1; i < words.size(); ++i) {
          const uint16_t predicted =
              static_cast<uint16_t>(
                  raw_crc(words[i], lsb != 0, poly) ^ offset);
          const uint16_t observed =
              orcsdr::js8::reconstruct::observed_crc(
                  words[i], reverse_crc != 0);
          if (predicted != observed) {
            valid = false;
            break;
          }
        }

        if (valid) {
          matches.push_back(Match{
              lsb ? "lsb" : "msb",
              reverse_crc != 0,
              poly,
              offset});
        }
      }
    }
  }

  std::printf("JS8_CRC_RECONSTRUCT words=%zu matches=%zu\n",
              words.size(), matches.size());
  for (const Match& match : matches) {
    std::printf(
        "direction=%s reverse_crc_bits=%u poly=0x%03X "
        "fixed_offset=0x%03X\n",
        match.direction,
        match.reverse_crc_bits ? 1u : 0u,
        static_cast<unsigned>(match.poly),
        static_cast<unsigned>(match.offset));
  }

  if (matches.empty()) return 1;
  if (matches.size() != 1) {
    std::printf(
        "WARNING reconstruction is not unique; add more diverse words "
        "or independent bit-orientation evidence.\n");
  }
  return 0;
}
