#pragma once
// SPDX-License-Identifier: AGPL-3.0-only
// ACARS application-text interpreter: turns a CRC-verified block's label and text into plain language. Pure functions over a
// MessageRecord, no allocation, no M5GFX / ESP-IDF dependency, host-tested (tests/acars_interpret_tests.cpp).
//
// Sources, all written from the field layouts (never copied): ARINC Specification 620 (squitter, general response, autotune,
// message sequence number, sub-labels, label table), plus formats observed on air and cross-checked against each other. A field
// that is not understood is never given a name: it is left out, or shown as received. The raw text is always shown separately.
#include <cstddef>
#include <cstdint>

#include "acars_dashboard_model.hpp"

namespace orcsdr::acars_ui {

constexpr size_t kInterpLines = 4;
constexpr size_t kInterpLineChars = 80;

struct Interpretation {
  bool recognised = false;                 // false: nothing here is understood, show the "not supplied" note
  bool decoded = false;                    // the text itself was parsed (false: only the label's meaning is known)
  char headline[48]{};                     // e.g. "FMC POSITION REPORT"
  char summary[kPreviewChars]{};           // one line for the message list
  char lines[kInterpLines][kInterpLineChars]{};
  size_t line_count = 0;
};

// Fills `out`. Returns out.recognised.
bool interpret(const MessageRecord& record, Interpretation& out);

// Plain-English name of a two character label as stored in a MessageRecord (the DEL of "_<DEL>" is stored as '?'), or nullptr.
const char* label_name(const char* label);
// Airline name for a flight identifier such as "AS0536", or nullptr when the prefix is not one we are sure of.
const char* airline_name(const char* flight);

}  // namespace orcsdr::acars_ui
