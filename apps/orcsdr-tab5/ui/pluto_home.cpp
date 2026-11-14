#include "pluto_home.hpp"

#include <cstring>

#include "pluto_source.hpp"

namespace orcsdr::pluto_home {

void fill(home::Snapshot& snapshot, bool dongle_ready) {
  if (!(pluto_source::active() || (!dongle_ready && pluto_source::ready()))) return;
  std::strncpy(snapshot.receiver, "PLUTO", sizeof(snapshot.receiver) - 1);
  snapshot.receiver[sizeof(snapshot.receiver) - 1] = '\0';
  snapshot.effective_sps = pluto_source::output_rate_sps();
  if (pluto_source::battery_low()) {
    // The host battery has been low over several readings: the chip says so and the stream keeps running. Only when the guard (stop mode) has
    // stopped the stream does the chip turn orange, the colour of "not ready".
    std::strncpy(snapshot.link_state, "LOW BATT", sizeof(snapshot.link_state) - 1);
    snapshot.link_state[sizeof(snapshot.link_state) - 1] = '\0';
    if (pluto_source::guard_stopped()) snapshot.driver_ready = false;
  }
}

}  // namespace orcsdr::pluto_home
