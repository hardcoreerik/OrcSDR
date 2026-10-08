#include "../apps/orcsdr-tab5/ui/ft8_codec.hpp"
#include "../apps/orcsdr-tab5/ui/ft8_ldpc.hpp"
#include "../apps/orcsdr-tab5/ui/ft8_ldpc_decode.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
using namespace orcsdr::ft8;
static_assert(sizeof(ldpc_decode::Workspace) <= 5000);

codec::MessageBits make_message() {
  codec::MessageBits message{};
  for (std::size_t i = 0; i < message.size(); ++i)
    message[i] = static_cast<uint8_t>(((i * 37u + 11u) ^ (i * i)) & 1u);
  return message;
}

ldpc_decode::LlrVector exact_llr(const codec::CodewordBits& codeword, float magnitude) {
  ldpc_decode::LlrVector llr{};
  for (std::size_t i = 0; i < llr.size(); ++i)
    llr[i] = codeword[i] ? -magnitude : magnitude;
  return llr;
}

void test_clean_early_exit() {
  const auto message = make_message();
  const auto codeword = ldpc::encode(message);
  auto llr = exact_llr(codeword, 8.0f);
  ldpc_decode::Workspace workspace{};
  ldpc_decode::Result result{};
  assert(ldpc_decode::decode(llr, &workspace, &result));
  assert(result.converged);
  assert(result.iterations == 0);
  assert(result.unsatisfied_checks == 0);
  assert(result.codeword == codeword);
  assert(result.message == message);
}

void test_corrects_deterministic_wrong_hard_decisions() {
  const auto message = make_message();
  const auto codeword = ldpc::encode(message);
  auto llr = exact_llr(codeword, 6.0f);
  for (int error = 0; error < 10; ++error) {
    const std::size_t bit = static_cast<std::size_t>((error * 37 + 7) % 174);
    llr[bit] = codeword[bit] ? 6.0f : -6.0f;
  }
  ldpc_decode::Workspace workspace{};
  ldpc_decode::Result result{};
  assert(ldpc_decode::decode(llr, &workspace, &result));
  assert(result.converged);
  assert(result.iterations > 0 && result.iterations <= 20);
  assert(result.unsatisfied_checks == 0);
  assert(result.message == message);
}

void test_reports_non_convergence() {
  const auto message = make_message();
  const auto codeword = ldpc::encode(message);
  auto llr = exact_llr(codeword, 6.0f);
  for (int error = 0; error < 15; ++error) {
    const std::size_t bit = static_cast<std::size_t>((error * 37 + 7) % 174);
    llr[bit] = codeword[bit] ? 6.0f : -6.0f;
  }
  ldpc_decode::Workspace workspace{};
  ldpc_decode::Result result{};
  assert(ldpc_decode::decode(llr, &workspace, &result));
  assert(!result.converged);
  assert(result.iterations == 20);
  assert(result.unsatisfied_checks > 0);
}

void test_invalid_inputs_and_config() {
  ldpc_decode::LlrVector llr{};
  ldpc_decode::Workspace workspace{};
  ldpc_decode::Result result{};
  assert(!ldpc_decode::decode(llr, nullptr, &result));
  assert(!ldpc_decode::decode(llr, &workspace, nullptr));

  ldpc_decode::Config config{};
  config.max_iterations = 0;
  assert(!ldpc_decode::decode(llr, &workspace, &result, config));
  config = {};
  config.normalization = 0.0f;
  assert(!ldpc_decode::decode(llr, &workspace, &result, config));
  config.normalization = 1.01f;
  assert(!ldpc_decode::decode(llr, &workspace, &result, config));
  config = {};
  config.max_abs_llr = 0.0f;
  assert(!ldpc_decode::decode(llr, &workspace, &result, config));
  config = {};
  llr[17] = std::numeric_limits<float>::quiet_NaN();
  assert(!ldpc_decode::decode(llr, &workspace, &result, config));
}

void test_max_iteration_bound_does_not_wrap() {
  ldpc_decode::LlrVector llr{};
  // Zero LLRs hard-decision to the all-zero valid codeword, so force a stable,
  // contradictory hard pattern that does not converge under the chosen graph.
  for (std::size_t i = 0; i < llr.size(); ++i)
    llr[i] = (i % 2u) ? -32.0f : 32.0f;
  ldpc_decode::Workspace workspace{};
  ldpc_decode::Result result{};
  ldpc_decode::Config config{};
  config.max_iterations = 255;
  assert(ldpc_decode::decode(llr, &workspace, &result, config));
  assert(!result.converged);
  assert(result.iterations == 255);
  assert(result.unsatisfied_checks > 0);
}

void test_repeated_weak_error_recovery() {
  uint32_t state = 0x6f2c91a5u;
  for (int trial = 0; trial < 100; ++trial) {
    codec::MessageBits message{};
    for (auto& bit : message) {
      state = state * 1664525u + 1013904223u;
      bit = static_cast<uint8_t>(state >> 31u);
    }
    const auto codeword = ldpc::encode(message);
    auto llr = exact_llr(codeword, 7.0f);
    for (int error = 0; error < 6; ++error) {
      state = state * 1664525u + 1013904223u;
      const std::size_t bit = state % codec::kCodewordBits;
      llr[bit] = codeword[bit] ? 0.75f : -0.75f;
    }
    ldpc_decode::Workspace workspace{};
    ldpc_decode::Result result{};
    assert(ldpc_decode::decode(llr, &workspace, &result));
    assert(result.converged);
    assert(result.message == message);
  }
}

}  // namespace

int main() {
  assert(orcsdr::ft8::ldpc_decode::self_check());
  test_clean_early_exit();
  test_corrects_deterministic_wrong_hard_decisions();
  test_reports_non_convergence();
  test_invalid_inputs_and_config();
  test_max_iteration_bound_does_not_wrap();
  test_repeated_weak_error_recovery();
  return 0;
}
