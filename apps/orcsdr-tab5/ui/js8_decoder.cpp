#include "js8_decoder.hpp"

#include "js8_fec.hpp"
#include "js8_ldpc_graph.hpp"

#include <cmath>
#include <cstring>

namespace orcsdr::js8::decoder {
namespace {

fec::Graph make_graph() {
  fec::Graph g;
  g.variable_count = ldpc_graph::kVariables;
  g.check_count = ldpc_graph::kChecks;
  g.edge_count = ldpc_graph::kEdges;
  g.check_offsets = ldpc_graph::kCheckOffsets;
  g.check_variables = ldpc_graph::kCheckVariables;
  g.variable_offsets = ldpc_graph::kVariableOffsets;
  g.variable_edges = ldpc_graph::kVariableEdges;
  return g;
}

struct AcceptContext {
  bool require_rendered;
};

bool accept_word(const uint8_t* codeword, void* ctx) {
  if (!codec::crc_valid(codeword)) return false;
  if (!static_cast<const AcceptContext*>(ctx)->require_rendered) return true;
  codec::Fields f;
  codec::extract_fields(codeword, &f);
  message::Rendered r;
  return message::render(f, &r);
}

void finish(const uint8_t* codeword, const uint8_t* hard, Method method, Result* r) {
  std::memcpy(r->codeword, codeword, codec::kCodewordBits);
  r->crc_valid = true;
  r->method = method;
  r->final_syndrome = codec::syndrome_weight(codeword);
  r->hard_corrections = 0;
  for (size_t i = 0; i < codec::kCodewordBits; ++i) r->hard_corrections = static_cast<uint16_t>(r->hard_corrections + (codeword[i] != hard[i]));
  codec::extract_fields(codeword, &r->fields);
  r->rendered = message::render(r->fields, &r->message);
}

}  // namespace

bool decode_llrs(const float llr[codec::kCodewordBits], const Config& config, Workspace* ws, Result* result) {
  if (llr == nullptr || ws == nullptr || result == nullptr) return false;
  *result = Result{};

  uint8_t hard[codec::kCodewordBits];
  for (size_t i = 0; i < codec::kCodewordBits; ++i) hard[i] = llr[i] < 0.0f ? 1u : 0u;
  result->initial_syndrome = codec::syndrome_weight(hard);

  if (result->initial_syndrome == 0 && codec::crc_valid(hard)) {
    finish(hard, hard, Method::hard, result);
    return true;
  }

  static const fec::Graph graph = make_graph();
  fec::Workspace fw;
  fw.variable_to_check = ws->variable_to_check;
  fw.check_to_variable = ws->check_to_variable;
  fw.posterior = ws->posterior;
  fw.edge_capacity = kBpEdges;
  fw.variable_capacity = codec::kCodewordBits;
  fec::Config fc;
  fc.max_iterations = config.bp_iterations;
  fec::Result fr;
  if (fec::decode(llr, codec::kCodewordBits, graph, &fw, &fr, fc)) {
    result->bp_iterations = fr.iterations;
    if (fr.converged && codec::crc_valid(fr.codeword.data())) {
      finish(fr.codeword.data(), hard, Method::bp, result);
      return true;
    }
  } else {
    result->bp_iterations = 0;
  }

  if (config.use_osd) {
    AcceptContext ctx{config.osd_require_rendered};
    osd::Config oc;
    oc.max_order = config.osd_order;
    osd::Result orr;
    if (osd::decode(llr, oc, &ws->osd, accept_word, &ctx, &orr)) {
      result->osd_tested = orr.tested;
      if (orr.discrepancy <= config.max_osd_discrepancy) {
        result->osd_order = orr.order;
        result->osd_discrepancy = orr.discrepancy;
        finish(orr.codeword.data(), hard, Method::osd, result);
        return true;
      }
    } else {
      result->osd_tested = orr.tested;
    }
  }
  return false;
}

bool decode(const float energy[codec::kChannelTones][8], const Config& config, Workspace* ws, Result* result) {
  if (energy == nullptr) return false;
  float llr[codec::kCodewordBits];
  codec::tone_llrs(energy, config.llr_gain, llr);
  return decode_llrs(llr, config, ws, result);
}

bool self_check() { return osd::self_check() && codec::self_check() && message::self_check(); }

}  // namespace orcsdr::js8::decoder
