#pragma once

// Glue between the Settings screen and the optional remote rtl_tcp source, kept out of
// main.cpp: it fills the three Settings fields and applies the two Settings actions.

#include "settings_app.hpp"

namespace orcsdr::settings_remote {

// Receiver source mode, saved server ("host" or "host:port") and a short, coarse status.
void fill(settings::State& state);

// Applies receiver_source_changed and remote_server_changed. Returns true if handled.
bool handle(const settings::Action& action);

}  // namespace orcsdr::settings_remote
