#include "js8-gf2.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using orcsdr::js8::reconstruct::Bits174;
using orcsdr::js8::reconstruct::CodeSpace;

namespace {
std::string bit_string(const Bits174& v) {
  std::string out(orcsdr::js8::reconstruct::kBits, '0');
  for (size_t i = 0; i < out.size(); ++i) if (v.get(i)) out[i] = '1';
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: %s <174-bit-codewords.txt> [--emit-h]\n", argv[0]);
    return 2;
  }
  const bool emit = argc == 3 && std::string(argv[2]) == "--emit-h";
  if (argc == 3 && !emit) return 2;

  std::ifstream input(argv[1]);
  if (!input) {
    std::perror(argv[1]);
    return 2;
  }
  std::vector<Bits174> rows;
  std::string line;
  size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    if (line.empty() || line[0] == '#') continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    Bits174 v{};
    if (!orcsdr::js8::reconstruct::parse_bits(line.c_str(), line.size(), &v)) {
      std::fprintf(stderr, "invalid 174-bit row at line %zu\n", line_no);
      return 2;
    }
    rows.push_back(v);
  }

  CodeSpace space{};
  if (!orcsdr::js8::reconstruct::analyze(rows.data(), rows.size(), &space)) return 1;
  size_t valid = 0;
  for (const Bits174& row : rows) valid += orcsdr::js8::reconstruct::satisfies(row, space);
  std::printf("JS8_FEC_RECONSTRUCT frames=%zu rank=%zu nullity=%zu parity_valid=%zu/%zu\n",
              rows.size(), space.rank, space.nullity, valid, rows.size());
  if (emit)
    for (size_t i = 0; i < space.nullity; ++i)
      std::printf("H[%zu]=%s\n", i, bit_string(space.parity_basis[i]).c_str());
  return valid == rows.size() ? 0 : 1;
}
