#pragma once
#include "digital_voice_dashboard_model.hpp"
namespace orcsdr::digital_voice_ui::demo {
enum class Case : uint8_t { disconnected, tuned_no_signal, candidate, dmr_replay, dmr_unknown_slot, p25_phase1, dstar_bits, ysf_bits, nxdn_planned, history_demo, signal_empty, discontinuity };
Snapshot make(Case id);
}
