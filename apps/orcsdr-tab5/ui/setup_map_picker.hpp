#pragma once

#include <cstdint>

namespace lgfx { inline namespace v1 { class LovyanGFX; } }

// "Choose on Map" -- the offline-first location method.
//
// Reads the world basemap from the read-only `orcmaps` flash partition, so it
// works on a device that has just been flashed from M5Burner with no SD card
// and no network. Everything below the ByteSource is ordinary OrcMaps: the
// partition feeds PmTilesReader through the same streaming decode and
// renderer the SD-backed maps use, and there is no embedded-map-specific
// rendering path.
//
// Interaction is a FIXED CENTRE CROSSHAIR: the map moves under a crosshair
// that never leaves the middle of the map area, and the selection is
// whatever the crosshair is over. Dragging pans the map; tapping a spot on
// the map brings that spot under the crosshair, so "tap where you are, then
// SET LOCATION" works too. Zoom never moves the selection.

namespace orcsdr::setup_map_picker {

// Which button ended the picker.
enum class Outcome : uint8_t { chosen, back, skipped, unavailable };

// Where the picker was opened from. First-run setup offers SKIP; Settings
// only has BACK (leave the stored location unchanged).
enum class Mode : uint8_t { first_run, settings };

struct Result {
  Outcome outcome = Outcome::unavailable;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint8_t zoom = 0;
};

// True if the embedded basemap partition exists and holds a readable PMTiles
// archive. Cheap enough to call during setup to decide whether to offer the
// map method at all; the wizard's basemap_available comes from this.
bool available();

// Label of the flash partition holding the basemap. OrcSDR owns the
// partition table, so the name lives here and is passed to OrcMaps at
// runtime rather than compiled into the library.
inline constexpr char kPartitionLabel[] = "orcmaps";

// Runs the picker until the user commits, goes back, or skips. Blocking: it
// owns the display and touch for its duration, which keeps the change to the
// main UI loop to a few lines.
//
// When `have_initial` is set the camera opens on that location -- a stored
// location being revisited, or an approximate network estimate. The result
// is still whatever the user confirms; an estimate is a starting camera
// position, never an accepted answer.
Result run(lgfx::v1::LovyanGFX& display, int32_t initial_latitude_e7,
           int32_t initial_longitude_e7, bool have_initial,
           Mode mode = Mode::first_run);

}  // namespace orcsdr::setup_map_picker
