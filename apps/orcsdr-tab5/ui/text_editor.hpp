#pragma once

#include <cstddef>

namespace orcsdr::text_editor {

enum class Result { none, changed, accepted, cancelled };

void begin(const char* title, const char* initial, size_t maximum_length,
           bool masked, const char* accept_label);
bool active();
const char* value();
void draw();
Result handle_touch(int x, int y);
void close();
bool self_check();

}  // namespace orcsdr::text_editor
