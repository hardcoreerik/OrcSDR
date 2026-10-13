#pragma once
// Plain-language SSTV status text, kept free of any display code so the wording rules can
// be tested on the host. The one rule that matters most: a mode identified from line-sync
// timing alone is never described as "VIS confirmed".
#include "sstv_receiver.hpp"
#include <cstddef>

namespace orcsdr::sstv_status {

enum class Tone : uint8_t { idle, info, good, warn };

struct Headline {
  char text[64] = {};
  Tone tone = Tone::idle;
};

// One line for the top of the RECEIVE page. `last` is the newest gallery image (or nullptr).
Headline headline(const orcsdr::sstv::Metrics& metrics, const orcsdr::sstv::Image* last);

// Short labels for one image (gallery tiles and the detail panel).
struct ImageChips {
  char identification[28] = {};  // "VIS 8", "SYNC LIKELY (87)", "MANUAL"
  char completeness[32] = {};    // "COMPLETE", "PARTIAL 214/240"
  char origin[40] = {};          // "STARTED AT TOP" / "JOINED IN PROGRESS - START ROW UNKNOWN"
  char ending[24] = {};          // "SYNC LOST", "NEW VIS", ...
};
void image_chips(const orcsdr::sstv::Image& image, ImageChips* out);

const char* strategy_label(orcsdr::sstv::AutoStrategy strategy, orcsdr::sstv::Mode manual);
const char* end_reason_label(orcsdr::sstv::EndReason reason);

}  // namespace orcsdr::sstv_status
