#include "../apps/orcsdr-tab5/ui/ft8_codec.hpp"
#include "../apps/orcsdr-tab5/ui/ft8_ldpc.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {
using namespace orcsdr::ft8;

void test_zero_word() {
  codec::MessageBits message{};
  const codec::CodewordBits codeword = ldpc::encode(message);
  for (uint8_t bit : codeword) assert(bit == 0);
  assert(ldpc::valid(codeword));
}

void test_systematic_and_parity() {
  codec::MessageBits message{};
  for (std::size_t i = 0; i < message.size(); ++i)
    message[i] = static_cast<uint8_t>(((i * 37u + 11u) ^ (i * i)) & 1u);
  const codec::CodewordBits codeword = ldpc::encode(message);
  for (std::size_t i = 0; i < message.size(); ++i) assert(codeword[i] == message[i]);
  assert(ldpc::valid(codeword));
}


void test_message_basis_vectors() {
  for (std::size_t message_bit = 0; message_bit < codec::kMessageBits; ++message_bit) {
    codec::MessageBits message{};
    message[message_bit] = 1;
    const codec::CodewordBits codeword = ldpc::encode(message);
    assert(codeword[message_bit] == 1u);
    assert(ldpc::valid(codeword));
  }
}

void test_single_bit_syndrome() {
  codec::MessageBits message{};
  for (std::size_t i = 0; i < message.size(); i += 3) message[i] = 1;
  const codec::CodewordBits good = ldpc::encode(message);
  for (std::size_t bit = 0; bit < good.size(); ++bit) {
    codec::CodewordBits bad = good;
    bad[bit] ^= 1u;
    assert(!ldpc::valid(bad));
    assert(ldpc::syndrome_weight(ldpc::syndrome(bad)) == 3u);
  }
}


void test_public_matrix_vector() {
  constexpr char kMessage[] =
      "0000000000000000000000000010000001001101111111001101110010001010000101000000101010101111001";
  constexpr char kParity[] =
      "10111110110001010110011001001101000100101110101000000101100001111110110011111001110";
  static_assert(sizeof(kMessage) - 1 == codec::kMessageBits);
  static_assert(sizeof(kParity) - 1 == ldpc::kCheckCount);
  codec::MessageBits message{};
  for (std::size_t i = 0; i < message.size(); ++i)
    message[i] = static_cast<uint8_t>(kMessage[i] - '0');
  const codec::CodewordBits codeword = ldpc::encode(message);
  for (std::size_t i = 0; i < ldpc::kCheckCount; ++i)
    assert(codeword[codec::kMessageBits + i] == static_cast<uint8_t>(kParity[i] - '0'));
  assert(ldpc::valid(codeword));
}

void test_deterministic_random_messages() {
  uint32_t state = 0x4f524346u;
  for (int vector = 0; vector < 1000; ++vector) {
    codec::MessageBits message{};
    for (auto& bit : message) {
      state = state * 1664525u + 1013904223u;
      bit = static_cast<uint8_t>(state >> 31u);
    }
    assert(ldpc::valid(ldpc::encode(message)));
  }
}
}  // namespace

int main() {
  assert(orcsdr::ft8::codec::self_check());
  assert(orcsdr::ft8::ldpc::self_check());
  test_zero_word();
  test_systematic_and_parity();
  test_message_basis_vectors();
  test_single_bit_syndrome();
  test_public_matrix_vector();
  test_deterministic_random_messages();
  return 0;
}
