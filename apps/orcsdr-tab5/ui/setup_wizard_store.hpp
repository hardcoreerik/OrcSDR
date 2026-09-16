#pragma once

#include "nvs_store.hpp"
#include "setup_wizard.hpp"

// Persistence for first-run setup, on top of OrcSDR's existing NVS settings
// rather than a second storage subsystem.
//
// Two things have to survive a reboot for setup to behave sanely:
//
//   1. Whether setup finished. A user who powers the device off part-way
//      through must come back into the wizard, not into an app that believes
//      it was configured.
//   2. What they had already chosen. Progress is written as setup advances,
//      not only at the end, so an interrupted run resumes instead of starting
//      over.
//
// The chosen location is NOT stored twice. It lives where the receiver
// location has always lived -- the adsb_loc_set / adsb_lat_e7 / adsb_lon_e7
// keys in the shared "orclink" namespace -- so there is one authoritative
// copy and the ADS-B radar, the map and the wizard cannot disagree. This
// module only adds the wizard's own progress keys alongside them.
//
// Everything here is user data and belongs in NVS: never in the embedded map
// partition, never in a PMTiles archive, never in a pack manifest. A normal
// firmware update rewrites bootloader, partition table and app but not NVS,
// so a stored location survives it; only an explicit full erase removes it.

namespace orcsdr::setup_wizard_store {

// Keys live in the same namespace as the rest of OrcSDR's settings. NVS
// limits a key to 15 characters.
inline constexpr char kVersionKey[] = "setup_ver";
inline constexpr char kCompletedKey[] = "setup_done";
inline constexpr char kStepKey[] = "setup_step";
inline constexpr char kSkipsKey[] = "setup_skips";
inline constexpr char kMethodKey[] = "setup_method";

// Shared with the ADS-B settings; the single authoritative receiver location.
inline constexpr char kLocationSetKey[] = "adsb_loc_set";
inline constexpr char kLatitudeKey[] = "adsb_lat_e7";
inline constexpr char kLongitudeKey[] = "adsb_lon_e7";

// Reads the stored record. A device that has never run setup, or whose keys
// are missing, yields a default record -- which NeedsSetup() reports as
// needing setup, so a blank device enters the wizard without any special
// case for "first ever boot".
setup_wizard::SetupRecord Load(NvsStore& preferences);

// Writes the record, including the location, so one call leaves NVS
// self-consistent. Callers persist at each step the wizard reports
// needs_persist, not only at the end.
bool Save(NvsStore& preferences, const setup_wizard::SetupRecord& record);

// Marks setup as not completed so the wizard runs again, WITHOUT discarding
// the stored location. This is what the Settings entry point calls: re-opening
// setup and backing out of it must never destroy a working configuration.
bool MarkIncomplete(NvsStore& preferences);

}  // namespace orcsdr::setup_wizard_store
