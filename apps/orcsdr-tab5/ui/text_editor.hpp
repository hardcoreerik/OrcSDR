#pragma once

#include <cstddef>

#include "keyboard_input.hpp"

namespace orcsdr::text_editor {

enum class Result { none, changed, accepted, cancelled };

void begin(const char* title, const char* initial, size_t maximum_length,
           bool masked, const char* accept_label);
bool active();
const char* value();
void draw();
Result handle_touch(int x, int y);
// Physical keyboard. Text keys and Backspace edit the field (repainting only the field);
// Enter/Esc return accept/cancel without closing so the owning screen can run the same path as
// its on-screen buttons: see accept_point()/cancel_point().
keyboard_input::EditAction handle_key(const keyboard_input::Key& key);
bool masked();
void accept_point(int* x, int* y);
void cancel_point(int* x, int* y);
void close();
bool self_check();

}  // namespace orcsdr::text_editor
