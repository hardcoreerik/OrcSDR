#include "js8-gf2.hpp"
#include "js8-sparse-parity.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace orcsdr::js8::reconstruct;

namespace {

Signature column_signature(const std::vector<Bits174>& basis, size_t column) {
  Signature signature{};
  for (size_t row = 0; row < basis.size(); ++row) {
    if (basis[row].get(column))
      signature.words[row / 64u] |= uint64_t{1} << (row % 64u);
  }
  return signature;
}

void print_indices(const Bits174& check) {
  bool first = true;
  for (size_t i = 0; i < kBits; ++i) {
    if (!check.get(i)) continue;
    std::printf("%s%zu", first ? "" : ",", i);
    first = false;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr,
                 "usage: %s <174-bit-codewords.txt> [max_checks=256]\n",
                 argv[0]);
    return 2;
  }

  size_t max_checks = 256;
  if (argc == 3) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || value == 0 || value > 4096)
      return 2;
    max_checks = static_cast<size_t>(value);
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::perror(argv[1]);
    return 2;
  }

  std::vector<Bits174> words;
  std::string line;
  size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    Bits174 word{};
    if (!parse_bits(line.c_str(), line.size(), &word)) {
      std::fprintf(stderr, "invalid 174-bit word at line %zu\n", line_no);
      return 2;
    }
    words.push_back(word);
  }
  if (words.empty()) return 2;

  CodeSpace code{};
  if (!analyze(words.data(), words.size(), &code)) return 1;

  std::vector<Bits174> basis;
  basis.reserve(code.rank);
  for (size_t pivot = 0; pivot < kBits; ++pivot)
    if (code.has_pivot[pivot]) basis.push_back(code.pivot_rows[pivot]);

  std::array<Signature, kBits> columns{};
  for (size_t column = 0; column < kBits; ++column)
    columns[column] = column_signature(basis, column);

  std::vector<Bits174> checks(max_checks);
  const size_t found = find_weight6(
      columns.data(), columns.size(), checks.data(), checks.size());

  size_t valid = 0;
  for (size_t i = 0; i < found; ++i) {
    bool ok = true;
    for (const Bits174& word : words) {
      if (dot(word, checks[i])) {
        ok = false;
        break;
      }
    }
    if (ok) ++valid;
  }

  CodeSpace check_space{};
  const bool have_space =
      found != 0 && analyze(checks.data(), found, &check_space);

  std::printf(
      "JS8_SPARSE_PARITY frames=%zu code_rank=%zu parity_dimension=%zu "
      "weight=6 found=%zu valid=%zu check_rank=%zu\n",
      words.size(), code.rank, code.nullity, found, valid,
      have_space ? check_space.rank : 0u);

  for (size_t i = 0; i < found; ++i) {
    std::printf("H6[%zu]=", i);
    print_indices(checks[i]);
    std::putchar('\n');
  }

  if (!have_space || check_space.rank < code.nullity) {
    std::printf(
        "INCOMPLETE sparse weight-6 checks span %zu of %zu parity "
        "dimensions; higher-weight search or more evidence is required.\n",
        have_space ? check_space.rank : 0u, code.nullity);
  }

  return valid == found ? 0 : 1;
}
