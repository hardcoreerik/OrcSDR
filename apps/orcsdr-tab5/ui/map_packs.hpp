#pragma once

#include <cstddef>
#include <cstdint>

// Map packs a user copied onto the SD card, found with OrcMaps' own pack discovery. The card layout is deliberately
// flat: <card>/orcmaps/<name>.pmtiles with <card>/orcmaps/<name>.manifest.json beside it. Nothing here downloads or
// writes anything; "installing" a pack is copying those two files. A pack that fails validation is listed with the
// reason instead of being silently ignored.
namespace orcsdr::map_packs {

constexpr size_t kViewMax = 8;                       // entries the UI can show
constexpr char kDirectory[] = "/sd/orcmaps";         // the VFS path; the card is mounted at /sd

struct Entry {
  char name[40]{};        // the pack's display name, or the manifest file name for a rejected pack
  char region[28]{};      // region name from the manifest (empty for a rejected pack)
  uint8_t min_zoom = 0;
  uint8_t max_zoom = 0;
  uint32_t size_kib = 0;
  bool valid = false;
  bool world = false;     // covers the whole world, so it can serve as the picker's basemap
  char note[32]{};        // "READY" or the rejection reason
};

struct Summary {
  bool scanned = false;
  bool directory_listed = false;   // false: no card, or no /orcmaps folder
  uint8_t pack_count = 0;          // valid packs found (may exceed kViewMax)
  uint8_t rejected_count = 0;
  uint8_t shown = 0;               // entries[0..shown) are filled
  Entry entries[kViewMax]{};
};

// What the map drawing code needs to choose and open a pack.
struct PackInfo {
  char path[112]{};       // the .pmtiles file (a VFS path)
  char credit[48]{};      // short attribution to show on the map ("" when none is required)
  uint8_t min_zoom = 0;
  uint8_t max_zoom = 0;
  float min_lat = 0, min_lon = 0, max_lat = 0, max_lon = 0;
  bool world = false;
};
constexpr size_t kInfoMax = 8;

// The valid packs from the last scan (up to kInfoMax); returns how many were copied.
size_t valid_packs(PackInfo* out, size_t capacity);
// Increments on every scan, so a cache of rendered maps knows when the installed packs changed.
uint32_t generation();

// Scans the card now. Blocking and file-system bound: call it from the normal application loop, never from the USB,
// audio or DSP paths.
void scan();
void scan_if_needed();
Summary summary();

// Path of the SD world pack with the deepest zoom, for the setup map picker to use instead of the embedded basemap.
// Returns false when no valid world pack is installed.
bool best_world_archive(char* path, size_t size, uint8_t* max_zoom);

// Pure helpers, host-tested.
bool covers_world(double min_lon, double min_lat, double max_lon, double max_lat);
void format_size(char* out, size_t size, uint32_t size_kib);   // "812 KB" / "9.3 MB" / "1.2 GB"
bool self_check();

}  // namespace orcsdr::map_packs
