#pragma once

#include <cstddef>
#include <cstdint>
#include "freq_keypad_layout.hpp"

// Shared direct-tuning numpad for the FM, AM and shortwave dashboards, so all
// three use the same large-key layout. The caller owns the entry buffer and
// validates the value on submit.
namespace orcsdr::freq_keypad {

enum class Result : uint8_t { none, changed, cancelled, submitted };

// Clears/draws within the viewport below clear_top (whole panel by default).
// Call self_check() after drawing to detect an insufficient usable viewport.
// title/hint/unit are copied.
void draw(int clear_top, uint16_t background, const char* title, const char* hint,
          const char* unit, const char* entry, const Rect* viewport = nullptr);
// Last drawn geometry, shared with input/focus and bounded update presentation.
const Layout& layout();
// Digits, '.', and backspace edit entry in place (and repaint the field);
// CANCEL and TUNE are reported for the caller to act on.
Result handle_touch(int32_t x, int32_t y, char* entry, size_t entry_size);
// Centre of a key ('0'-'9', '.', '\b' for backspace) or of the action buttons, for callers that
// press them on the user's behalf (a physical keyboard). Returns false for an unknown key.
bool key_point(char key, int* x, int* y);
void cancel_point(int* x, int* y);
void tune_point(int* x, int* y);
bool self_check();

}  // namespace orcsdr::freq_keypad
