#pragma once
// The OrcSDR serial command interface (docs/API_SERIAL_CLI.md) on the P4 console. Same line
// protocol, same PAIR/AUTH HMAC handshake, same reply formats as the Tab5's process_command(),
// for the commands this target implements; commands it does not implement stay silent, as the
// Tab5 documents for unknown input. `RTL_HELP` lists what is implemented.
#include "orcsdr_gfx/scaled_gfx.hpp"

// Starts the command task on the console UART. `display` is used for FT8 SNAP screenshots.
bool serial_cli_start(orcsdr::gfx::ScaledDisplay* display);
