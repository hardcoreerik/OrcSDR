#pragma once
// PlutoSDR source over USB (see pluto_service.cpp). Compiled to a no-op unless CONFIG_ORCSDR_PLUTO_SOURCE is set.
namespace orcsdr::pluto {
// Starts the task that waits for a Pluto and, when one is attached and no RTL-SDR is streaming, runs it as receiver 0.
bool start();
}  // namespace orcsdr::pluto
