#pragma once

#include <cstddef>
#include <cstdint>

// First-run setup for a device that was just 1-click flashed from M5Burner.
//
// The governing assumption is the worst realistic first boot: no SD card, no
// Wi-Fi credentials, no PC, and a user who has never seen the product. Every
// step must therefore be completable, or skippable, from that state.
//
// That rules out the obvious location methods. A postal-code or city lookup
// goes through Nominatim and a "use my location" estimate goes through
// ipwho.is, so all three need the network; the Tab5 has no GNSS receiver at
// all, so there is no on-device fix to fall back to. What remains offline is
// a pin dropped on a locally stored basemap, and typed coordinates. The pin
// is the primary path and the basemap ships in firmware, which is what makes
// this reachable with nothing but a flashed board.
//
// The wizard is free of M5Unified, OrcMaps and ESP-IDF: it owns the step
// sequence, method availability, validation and the map-coverage decision.
// Drawing belongs to the UI layer, projection to orcmap::LocationPicker, and
// persistence to main.cpp.

namespace orcsdr::setup_wizard {

// Network precedes location so that a user who does connect gets every
// location method offered, rather than discovering later that three of them
// were unavailable. Nothing after the location step is required.
enum class Step : uint8_t {
  welcome,
  network,
  location,
  maps,
  complete,
};

constexpr uint8_t kStepCount = 5;

enum class LocationMethod : uint8_t {
  none,
  map_pin,           // offline: pin on the embedded or SD basemap
  coordinates,       // offline: typed latitude/longitude
  postal_code,       // network: Nominatim
  city_address,      // network: Nominatim
  network_estimate,  // network: ipwho.is (NOT a GNSS fix -- there is no GNSS)
};

// The map-area tiers offered on the maps step, as radii in statute miles.
enum class MapTier : uint8_t { none, local, regional, travel };

constexpr uint16_t kLocalRadiusMiles = 50;
constexpr uint16_t kRegionalRadiusMiles = 150;
constexpr uint16_t kTravelRadiusMiles = 300;

// Coordinates are 1e-7 degrees to match orcsdr::adsb::Settings and
// orcsdr::location_estimate, so a location moves through the wizard without a
// lossy trip through float.
constexpr int32_t kLatitudeLimitE7 = 900000000;
constexpr int32_t kLongitudeLimitE7 = 1800000000;

// A pack must reach at least this zoom to count as local map coverage. A
// world basemap contains every point on Earth and resolves none of them, so
// treating containment as coverage would tell a user their map is fine while
// it cannot draw the town they are sitting in.
constexpr uint8_t kMinDetailZoom = 11;

constexpr uint8_t kRecommendedMinZoom = 1;
constexpr uint8_t kRecommendedMaxZoom = 13;

constexpr size_t kPackNameSize = 40;
constexpr size_t kMessageSize = 72;

// One installed pack as the device discovered it. Filled in by the UI layer
// from orcmap::PackCatalog so this header stays free of OrcMaps.
struct PackCoverage {
  char name[kPackNameSize]{};
  int32_t min_lat_e7 = 0;
  int32_t min_lon_e7 = 0;
  int32_t max_lat_e7 = 0;
  int32_t max_lon_e7 = 0;
  uint8_t min_zoom = 0;
  uint8_t max_zoom = 0;
};

// What still has to happen before this device has a local map. Computed
// rather than phrased as generic advice, because the radius that matters is
// the one OrcSDR will actually draw.
struct Recommendation {
  bool needed = false;
  uint16_t radius_km = 0;
  uint8_t min_zoom = kRecommendedMinZoom;
  uint8_t max_zoom = kRecommendedMaxZoom;
  char name[kPackNameSize]{};
};

struct State {
  Step step = Step::welcome;

  // Environment the wizard was started in.
  bool network_connected = false;
  // A basemap good enough to drop a pin on: embedded in firmware, or a
  // world pack found on SD. Without it the location step has only typed
  // coordinates left when offline.
  bool basemap_available = false;
  bool sd_present = false;

  // Per-step skip record, so a later screen can tell "not done" from
  // "declined" and the app can re-offer it.
  bool network_skipped = false;
  bool location_skipped = false;
  bool maps_skipped = false;
  bool quick_started = false;

  LocationMethod method = LocationMethod::none;
  bool location_valid = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;

  uint16_t radar_range_nm = 25;
  MapTier tier = MapTier::none;

  bool coverage_evaluated = false;
  bool covered = false;
  char covering_pack[kPackNameSize]{};
  Recommendation recommendation{};
  char message[kMessageSize]{};
};

enum class Outcome : uint8_t {
  ok,
  blocked,        // the step's precondition is not satisfied yet
  needs_persist,  // state was accepted and must be written to NVS
  finished,
};

// Conditions the device is in when setup starts.
struct Environment {
  bool network_connected = false;
  bool basemap_available = false;
  bool sd_present = false;
  uint16_t radar_range_nm = 25;
};

class Wizard {
 public:
  void begin(const Environment& environment);

  // The network step may be completed either way; radio reception does not
  // depend on it and neither does the location step's primary method.
  void set_network_connected(bool connected);

  bool set_pin(int32_t latitude_e7, int32_t longitude_e7);
  bool set_manual(int32_t latitude_e7, int32_t longitude_e7);
  // Result of a network lookup (postal code, address, or IP estimate).
  bool set_looked_up(LocationMethod method, int32_t latitude_e7,
                     int32_t longitude_e7);

  void set_radar_range_nm(uint16_t radar_range_nm);
  bool select_tier(MapTier tier);

  void evaluate_coverage(const PackCoverage* packs, size_t count);

  Outcome advance();
  // Declines the current step and moves on. Every step is skippable: a user
  // who cannot complete one must never be trapped on it.
  Outcome skip();
  bool back();

  // "QUICK START": straight to the app, everything declined.
  Outcome quick_start();

  State state() const { return state_; }
  bool complete() const { return state_.step == Step::complete; }

  size_t provision_command(char* out, size_t size,
                           const char* source_manifest) const;

  static bool self_check();

 private:
  void recommend();
  void set_message(const char* text);

  State state_{};
};

// True if the method cannot work without a network connection.
bool LocationMethodNeedsNetwork(LocationMethod method);

// Whether a method can be offered right now. The UI uses this to disable
// options rather than let a user pick one that will fail.
bool LocationMethodAvailable(LocationMethod method, bool network_connected,
                             bool basemap_available);

// Count of methods currently usable. Zero means the location step can only
// be skipped, which the wizard must still allow.
uint8_t AvailableLocationMethodCount(bool network_connected,
                                     bool basemap_available);

uint16_t TierRadiusMiles(MapTier tier);
uint16_t TierRadiusKm(MapTier tier);

// Nautical miles are what OrcSDR's radar range uses; packs are cut in
// kilometres.
uint16_t radius_km_for_range_nm(uint16_t range_nm);

bool coordinates_valid(int32_t latitude_e7, int32_t longitude_e7);

const char* StepName(Step step);

}  // namespace orcsdr::setup_wizard
