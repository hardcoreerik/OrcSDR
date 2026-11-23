// Re-uses the D1 integration suite's synthetic 4FSK generator (same TU trick; main renamed). Synthetic IQ only.
#define main dmr_bridge_test_main
#include "../../components/orcsdr_dmr/d1_integration/tests/test_d1_bridge.cpp"
#undef main
#include <vector>
std::vector<float> dv_test_dmr_iq48() {
  const auto iq = synthesize(make_stream());
  std::vector<float> out;
  out.reserve(iq.size() * 2);
  for (const auto& s : iq) { out.push_back(s.re); out.push_back(s.im); }
  return out;
}
