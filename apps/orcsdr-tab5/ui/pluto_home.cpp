#include "pluto_home.hpp"

#include <cstring>

#include "pluto_source.hpp"

namespace orcsdr::pluto_home {

bool applies(bool dongle_ready) { return pluto_source::active() || (!dongle_ready && pluto_source::ready()); }

void fill(home::Snapshot& snapshot, bool dongle_ready) {
  if (!applies(dongle_ready)) return;
  std::strncpy(snapshot.receiver, "PLUTO", sizeof(snapshot.receiver) - 1);
  snapshot.receiver[sizeof(snapshot.receiver) - 1] = '\0';
  snapshot.effective_sps = pluto_source::output_rate_sps();
  // The screen may show a frequency the Pluto cannot tune (a saved one from a dongle, or one typed on the keypad). The stream is stopped for it and the
  // Home waterfall says so, using the same warning screen the E4000 uses. The range is the driver's, never the app's.
  if (!pluto_source::tunable(snapshot.frequency_hz)) snapshot.tuner_out_of_range = true;
  if (pluto_source::battery_low()) {
    // The HOST's battery has been low over several readings. A note only: it changes no colour and no behaviour, and says nothing about how the
    // Pluto itself is powered, which the host cannot see.
    std::strncpy(snapshot.link_state, "HOST BATT", sizeof(snapshot.link_state) - 1);
    snapshot.link_state[sizeof(snapshot.link_state) - 1] = '\0';
  }
}

}  // namespace orcsdr::pluto_home
