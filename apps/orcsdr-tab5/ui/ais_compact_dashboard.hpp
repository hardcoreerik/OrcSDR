#pragma once
// Compact AIS dashboard (ais_compact_dashboard.cpp): the orcsdr::ais_ui contract of ais_dashboard.hpp for small views, plus the viewport call
// the application makes before ais_app::open(). A build links either this or ais_dashboard.cpp.
#include "ais_compact_layout.hpp"
#include "ais_dashboard.hpp"

namespace orcsdr::ais_ui {
bool set_viewport(compact::Rect viewport);   // validates the view, moves the shared header, allocates the (PSRAM) state
}  // namespace orcsdr::ais_ui
