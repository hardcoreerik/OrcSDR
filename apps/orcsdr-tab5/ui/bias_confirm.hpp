#pragma once
// Confirmation popup for turning the antenna-port power (bias tee) OFF from the red "BIAS ON" rectangle shown on every screen.
// Draw/touch only: it never talks to the radio. The caller turns bias off when touch() returns turn_off. It can only ask to turn
// bias OFF; turning it on stays with Home's BIAS popup, RF Lab and the serial command (owner rule: bias is never enabled by anything
// but a deliberate owner request).
#include <cstdint>

namespace orcsdr::bias_confirm {

enum class Result : uint8_t { none, closed, turn_off };

// The red BIAS ON rectangle in the shared header.
bool rect_hit(int32_t x, int32_t y);
// Draws the popup over whatever is on screen (the pixels under it are kept and restored on close).
void open();
bool active();
// While active, every touch belongs to the popup. CONFIRM: turn_off, CANCEL or a touch outside: closed. Both restore the screen.
Result touch(int32_t x, int32_t y);
// Closes without a choice (screen change) and frees the saved pixels.
void close();

}  // namespace orcsdr::bias_confirm
