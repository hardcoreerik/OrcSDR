// Validates the generic, graph-driven JS8 FEC engine (js8_fec) and its graph format against a code whose truth is independently known:
// the FT8 LDPC(174,91) used by the FT8/FT4 decoder. This is preparatory validation of the ENGINE and the graph representation only.
// It does not use the FT8 matrix or the FT8 CRC for JS8: JS8's own parity graph and CRC arrive from the protocol specification.
//
// Method: build a js8::fec::Graph from the FT8 parity graph (ft8_ldpc_graph.hpp), then for several noise levels decode many noisy
// codewords with BOTH engines. Checks:
//   1. the graph built here is valid and has the right shape (83 checks, 174 variables, 522 edges);
//   2. every codeword the generic engine reports as converged satisfies all 83 checks (FT8's own syndrome checker agrees) and, at high
//      SNR, equals the transmitted codeword (no wrong-codeword convergence at the noise levels where decoding is easy);
//   3. at moderate noise the generic engine's success rate is close to the FT8 engine's (same algorithm family, so a large gap means a bug);
//   4. unconverged results are reported as unconverged, never as success.
#include "ft8_codec.hpp"
#include "ft8_ldpc.hpp"
#include "ft8_ldpc_decode.hpp"
#include "ft8_ldpc_graph.hpp"
#include "js8_fec.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

namespace {

namespace f8 = orcsdr::ft8;
namespace jf = orcsdr::js8::fec;

struct OwnedGraph {
  std::vector<uint16_t> check_offsets, check_variables, variable_offsets, variable_edges;
  jf::Graph graph;
};

// Check-major edge numbering: edge e is the e-th entry of check_variables. variable_edges lists, per variable, its edges.
OwnedGraph build_graph() {
  constexpr size_t kVars = f8::codec::kCodewordBits;
  constexpr size_t kChecks = f8::ldpc::kCheckCount;
  std::vector<std::vector<uint16_t>> members(kChecks);
  for (size_t v = 0; v < kVars; ++v)
    for (uint8_t c : f8::ldpc_graph::kColumnChecks[v]) members[c].push_back(static_cast<uint16_t>(v));
  OwnedGraph g;
  g.check_offsets.push_back(0);
  std::vector<std::vector<uint16_t>> edges_of(kVars);
  for (size_t c = 0; c < kChecks; ++c) {
    for (uint16_t v : members[c]) {
      edges_of[v].push_back(static_cast<uint16_t>(g.check_variables.size()));
      g.check_variables.push_back(v);
    }
    g.check_offsets.push_back(static_cast<uint16_t>(g.check_variables.size()));
  }
  g.variable_offsets.push_back(0);
  for (size_t v = 0; v < kVars; ++v) {
    for (uint16_t e : edges_of[v]) g.variable_edges.push_back(e);
    g.variable_offsets.push_back(static_cast<uint16_t>(g.variable_edges.size()));
  }
  g.graph = jf::Graph{static_cast<uint16_t>(kVars), static_cast<uint16_t>(kChecks), static_cast<uint16_t>(g.check_variables.size()),
                      g.check_offsets.data(), g.check_variables.data(), g.variable_offsets.data(), g.variable_edges.data()};
  return g;
}

f8::codec::CodewordBits random_codeword(std::mt19937& rng) {
  f8::codec::PayloadBits payload{};
  for (auto& b : payload) b = static_cast<uint8_t>(rng() & 1u);
  return f8::ldpc::encode(f8::codec::append_crc(payload));
}

}  // namespace

int main() {
  assert(jf::self_check());
  OwnedGraph owned = build_graph();
  assert(jf::graph_valid(owned.graph));
  assert(owned.graph.variable_count == 174 && owned.graph.check_count == 83 && owned.graph.edge_count == 522);

  std::vector<float> storage(jf::workspace_bytes(owned.graph) / sizeof(float));
  jf::Workspace workspace{};
  assert(jf::bind_workspace(storage.data(), storage.size() * sizeof(float), owned.graph, &workspace));

  std::mt19937 rng(8675309);
  std::normal_distribution<float> gauss(0.0f, 1.0f);
  const float sigmas[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f};   // BPSK with unit amplitude: LLR = 2 y / sigma^2
  size_t wrong_codeword = 0;
  for (float sigma : sigmas) {
    const int trials = 300;
    int generic_ok = 0, ft8_ok = 0, both = 0;
    for (int t = 0; t < trials; ++t) {
      const auto codeword = random_codeword(rng);
      std::array<float, 174> llr{};
      for (size_t i = 0; i < 174; ++i) {
        const float tx = codeword[i] ? -1.0f : 1.0f;   // LLR > 0 means bit 0
        llr[i] = 2.0f * (tx + sigma * gauss(rng)) / (sigma * sigma);
      }

      jf::Result gr{};
      const bool g_converged = jf::decode(llr.data(), llr.size(), owned.graph, &workspace, &gr) && gr.converged;
      f8::codec::CodewordBits g_bits{};
      for (size_t i = 0; i < 174; ++i) g_bits[i] = gr.codeword[i];
      if (g_converged) {
        assert(f8::ldpc::valid(g_bits));       // FT8's independent syndrome checker agrees
        assert(gr.unsatisfied_checks == 0);
        if (g_bits != codeword) ++wrong_codeword;
        ++generic_ok;
      } else {
        assert(gr.unsatisfied_checks != 0 || !gr.converged);   // never "success" without parity
      }

      f8::ldpc_decode::LlrVector lv{};
      std::copy(llr.begin(), llr.end(), lv.begin());
      f8::ldpc_decode::Workspace ws{};
      f8::ldpc_decode::Result fr{};
      const bool f_ok = f8::ldpc_decode::decode(lv, &ws, &fr, f8::ldpc_decode::Config{}) && fr.converged;
      if (f_ok) ++ft8_ok;
      if (g_converged && f_ok) ++both;
    }
    std::printf("sigma %.1f: generic engine %d/%d converged, FT8 engine %d/%d, both %d\n", static_cast<double>(sigma), generic_ok, trials, ft8_ok, trials, both);
    if (sigma <= 0.6f) assert(generic_ok == trials);   // easy noise: everything decodes
    // Same algorithm family on the same graph: the two engines must succeed on (almost) exactly the same words.
    assert(generic_ok * 100 >= ft8_ok * 97 && ft8_ok * 100 >= generic_ok * 97);
    assert(both * 100 >= std::max(generic_ok, ft8_ok) * 97);
  }
  assert(wrong_codeword <= 1);   // at these levels a converged word is the transmitted word (a rare valid-but-wrong word is tolerated at most once)
  std::printf("js8_fec_ft8_vectors_tests: PASS (wrong-codeword convergences: %zu)\n", wrong_codeword);
  return 0;
}
