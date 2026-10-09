#include "../tools/js8-sparse-parity.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace orcsdr::js8::reconstruct;

namespace {

Signature basis_bit(size_t bit) {
  Signature signature{};
  signature.words[bit / 64u] |= uint64_t{1} << (bit % 64u);
  return signature;
}

}  // namespace

int main() {
  std::array<Signature, 12> columns{};
  columns[0] = basis_bit(0);
  columns[1] = basis_bit(1);
  columns[2] = basis_bit(2);
  columns[3] = basis_bit(3);
  columns[4] = basis_bit(4);
  columns[5] =
      columns[0] ^ columns[1] ^ columns[2] ^ columns[3] ^ columns[4];

  for (size_t i = 6; i < columns.size(); ++i)
    columns[i] = basis_bit(i - 1);

  std::array<Bits174, 16> checks{};
  const size_t count = find_weight6(
      columns.data(), columns.size(), checks.data(), checks.size());
  assert(count >= 1);

  Bits174 expected{};
  for (size_t i = 0; i < 6; ++i) expected.set(i);

  bool seen = false;
  for (size_t i = 0; i < count; ++i)
    if (checks[i].words == expected.words) seen = true;
  assert(seen);

  const Triple a{0, 1, 2};
  const Triple b{3, 4, 5};
  const Triple c{2, 6, 7};
  assert(disjoint(a, b));
  assert(!disjoint(a, c));

  std::puts("JS8 sparse parity search tests: PASS");
  return 0;
}
