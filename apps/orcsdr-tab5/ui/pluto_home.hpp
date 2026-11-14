#pragma once

// What a PlutoSDR changes on the Home screen: the receiver tag (PLUTO instead of RTL-SDR, in the header chip and the footer), the rate the Pluto's
// stream delivers, and a LOW BATT note when the host battery has been low over several readings. Kept out of main.cpp on purpose.

#include "home_dashboard.hpp"

namespace orcsdr::pluto_home {

// Fills the receiver-related fields of a Home snapshot when a Pluto is the local receiver: its stream is running, or it is attached and no
// dongle is ready (a dongle always wins). Does nothing otherwise, so the caller can call it unconditionally.
void fill(home::Snapshot& snapshot, bool dongle_ready);

}  // namespace orcsdr::pluto_home
