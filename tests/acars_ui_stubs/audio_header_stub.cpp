// Host stand-in for dashboard_audio_control.cpp: the shared header is owned (and self-checked) elsewhere,
// so the ACARS tests record nothing for it. Only what acars_dashboard.cpp calls is provided.
#include "dashboard_audio_control.hpp"

namespace orcsdr::audio_header {
void draw_brand(const char*) {}
}  // namespace orcsdr::audio_header
