#pragma once

#include <cstddef>
#include <cstdint>

// First-run setup, offline by construction.
//
// OrcSDR's existing location paths both need the network: an address search
// via Nominatim, or an approximate fix from an IP lookup. A field radio that
// cannot establish its own position without Wi-Fi is not much of a field
// radio, so this wizard's location step is a pin dropped on a locally stored
// map and nothing else. No lookup, no fallback to one.
//
// The wizard is deliberately free of M5Unified, OrcMaps and ESP-IDF. It owns
// the sequence, the validation and the decision about what map data the
// chosen location still needs; the Tab5 UI layer owns drawing, the OrcMaps
// viewport owns projection, and main.cpp owns persistence. That split is
// what makes the whole flow testable on a host.

namespace orcsdr::setup_wizard {

enum class Step : uint8_t {
  welcome,
  pick_location,
  confirm_location,
  map_coverage,
  complete,
};

// How the stored location was obtained. Recorded because a hand-typed
// coordinate and a pin dropped on a map deserve different trust: one can be
// a typo of arbitrary magnitude, the other is bounded by the map the user
// was looking at.
enum class LocationSource : uint8_t { none, map_pin, manual_entry };

// Coordinates are 1e-7 degrees to match the integer convention already used
// by orcsdr::adsb::Settings and orcsdr::location_estimate, so a location can
// move through the wizard without a lossy round trip through float.
constexpr int32_t kLatitudeLimitE7 = 900000000;
constexpr int32_t kLongitudeLimitE7 = 1800000000;

// A pack must reach at least this zoom to count as local map coverage. A
// world overview pack contains every point on Earth and is the right data
// for dropping a pin, but treating that as "covered" would leave a user with
// a receiver whose map cannot resolve the town they are sitting in.
constexpr uint8_t kMinDetailZoom = 11;

// The zoom range the provisioner is asked for. z1 keeps the pack usable when
// zoomed all the way out; z13 is the established detail target.
constexpr uint8_t kRecommendedMinZoom = 1;
constexpr uint8_t kRecommendedMaxZoom = 13;

constexpr size_t kPackNameSize = 40;
constexpr size_t kMessageSize = 72;

// One installed pack as the device discovered it on the card. Mirrors the
// fields of an OrcMaps manifest the wizard actually reasons about; the UI
// layer fills these in from orcmap::PackCatalog so this header stays free of
// any OrcMaps dependency.
struct PackCoverage {
  char name[kPackNameSize]{};
  int32_t min_lat_e7 = 0;
  int32_t min_lon_e7 = 0;
  int32_t max_lat_e7 = 0;
  int32_t max_lon_e7 = 0;
  uint8_t min_zoom = 0;
  uint8_t max_zoom = 0;
};

// What the operator has to do on a PC before this device has a local map.
// The wizard computes it rather than printing generic advice, because the
// radius that matters is the one OrcSDR will actually draw.
struct Recommendation {
  bool needed = false;
  uint16_t radius_km = 0;
  uint8_t min_zoom = kRecommendedMinZoom;
  uint8_t max_zoom = kRecommendedMaxZoom;
  char name[kPackNameSize]{};
};

struct State {
  Step step = Step::welcome;
  // False when no pack on the card can be used to drop a pin. The wizard
  // then routes to manual entry instead of presenting an empty map.
  bool overview_available = false;
  LocationSource source = LocationSource::none;
  bool location_valid = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint16_t radar_range_nm = 25;
  bool coverage_evaluated = false;
  bool covered = false;
  char covering_pack[kPackNameSize]{};
  Recommendation recommendation{};
  char message[kMessageSize]{};
};

// Result of a step transition, so the caller knows whether to persist.
enum class Outcome : uint8_t {
  ok,
  blocked,        // the step's precondition is not satisfied yet
  needs_persist,  // the location was accepted and must be written to NVS
  finished,
};

class Wizard {
 public:
  // `overview_available` says whether a pin can be dropped on a real map.
  void begin(bool overview_available, uint16_t radar_range_nm);

  // Both accept 1e-7 degrees and reject anything outside the coordinate
  // limits. A pin also rejects the exact null island default, because
  // 0,0 arriving from a map interaction means the pin was never moved.
  bool set_pin(int32_t latitude_e7, int32_t longitude_e7);
  bool set_manual(int32_t latitude_e7, int32_t longitude_e7);

  void set_radar_range_nm(uint16_t radar_range_nm);

  // Compares the chosen location against installed packs and fills in the
  // recommendation. Safe to call with no packs.
  void evaluate_coverage(const PackCoverage* packs, size_t count);

  Outcome advance();
  bool back();

  State state() const { return state_; }
  bool complete() const { return state_.step == Step::complete; }

  // Renders the exact command an operator runs on a PC. Returns the number
  // of characters that would be written, excluding the terminator, so a
  // caller can detect truncation.
  size_t provision_command(char* out, size_t size,
                           const char* source_manifest) const;

  static bool self_check();

 private:
  void recommend();

  State state_{};
};

// Nautical miles are what OrcSDR's radar range is expressed in; packs are cut
// in kilometres. Exposed for tests and for the UI's own labelling.
uint16_t radius_km_for_range_nm(uint16_t range_nm);

bool coordinates_valid(int32_t latitude_e7, int32_t longitude_e7);

}  // namespace orcsdr::setup_wizard
