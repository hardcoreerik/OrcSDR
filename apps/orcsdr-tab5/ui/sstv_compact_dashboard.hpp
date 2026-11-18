#pragma once
// Compact SSTV dashboard (sstv_compact_dashboard.cpp): the orcsdr::sstv ui_* contract of sstv_dashboard.hpp for small views, plus the
// viewport call the application makes before sstv_app::open(). A build links either this or sstv_dashboard.cpp.
#include "sstv_compact_layout.hpp"
#include "sstv_dashboard.hpp"

namespace orcsdr::sstv {
bool ui_set_viewport(compact::Rect viewport);
// The application's measure of the IQ level, dBFS (the shared sstv_app does not supply one); without it the RF figure is not shown.
void ui_set_rf_level_source(float (*level_dbfs)());   // validates the view, moves the shared header, allocates the (PSRAM) state
}  // namespace orcsdr::sstv
