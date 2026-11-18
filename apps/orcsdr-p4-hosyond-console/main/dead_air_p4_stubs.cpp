// P4 console stand-ins for the two Tab5-hardware modules of Dead Air (dead_air_env_m5.cpp: the Tab5's Port A through M5Unified's
// Ex_I2C; dead_air_mic.cpp: the Tab5's built-in microphone). This target has neither wired in, and both report that plainly: the
// environment bus is "not available" and the microphone "cannot start", so Dead Air shows no environment readings and no mic clips.
#include "dead_air_env_m5.hpp"
#include "dead_air_mic.hpp"

namespace orcsdr::dead_air::env {
Bus* port_a_bus() { return nullptr; }   // the app treats nullptr as "external I2C bus not available"
}  // namespace orcsdr::dead_air::env

namespace orcsdr::dead_air_mic {
namespace {
const char* const kError = "no microphone here";
}
bool start(Sink) { return false; }
void stop() {}
bool running() { return false; }
uint32_t flat_blocks() { return 0; }
Stats stats() {
  Stats s;
  s.error = kError;
  return s;
}
}  // namespace orcsdr::dead_air_mic
