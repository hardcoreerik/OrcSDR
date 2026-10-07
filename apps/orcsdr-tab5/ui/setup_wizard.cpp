#include "setup_wizard.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::setup_wizard {
namespace {

// A pack should cover at least what the radar draws, plus a margin so the
// map does not end exactly at the edge of the range ring.
constexpr double kKmPerNauticalMile = 1.852;
constexpr double kKmPerStatuteMile = 1.609344;
constexpr double kCoverageMargin = 1.25;
constexpr uint16_t kMinRadiusKm = 25;
// A 300 mile travel tier is 483 km, so the ceiling has to clear it.
constexpr uint16_t kMaxRadiusKm = 800;

void copy_text(char* out, size_t size, const char* text) {
  if (out == nullptr || size == 0) return;
  if (text == nullptr) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, size, "%s", text);
}

// Copies from a fixed-size source field that may not be terminated.
//
// PackCoverage.name is filled by the UI layer from a manifest on a removable
// card. If that copy fills the field with no room for a terminator, treating
// it as a C string reads the struct's following coordinate bytes as text.
void copy_field(char* out, size_t out_size, const char* src, size_t src_size) {
  if (out == nullptr || out_size == 0) return;
  if (src == nullptr) {
    out[0] = '\0';
    return;
  }
  size_t length = 0;
  while (length < src_size && src[length] != '\0') ++length;
  if (length > out_size - 1) length = out_size - 1;
  std::memcpy(out, src, length);
  out[length] = '\0';
}

bool contains(const PackCoverage& pack, int32_t lat_e7, int32_t lon_e7) {
  return lat_e7 >= pack.min_lat_e7 && lat_e7 <= pack.max_lat_e7 &&
         lon_e7 >= pack.min_lon_e7 && lon_e7 <= pack.max_lon_e7;
}

uint16_t clamp_radius(double km) {
  if (km <= static_cast<double>(kMinRadiusKm)) return kMinRadiusKm;
  if (km >= static_cast<double>(kMaxRadiusKm)) return kMaxRadiusKm;
  // Round up: a pack that stops just short of the range ring is the one
  // failure this calculation exists to prevent.
  return static_cast<uint16_t>(km + 0.999);
}

// Accepting a location is the same bookkeeping whichever method produced it,
// and any earlier coverage verdict belongs to the old coordinates.
void accept_location(State* state, LocationMethod method, int32_t lat_e7,
                     int32_t lon_e7) {
  state->latitude_e7 = lat_e7;
  state->longitude_e7 = lon_e7;
  state->method = method;
  state->location_valid = true;
  state->location_skipped = false;
  state->coverage_evaluated = false;
  state->covered = false;
  state->covering_pack[0] = '\0';
  state->recommendation = Recommendation{};
}

}  // namespace

bool coordinates_valid(int32_t latitude_e7, int32_t longitude_e7) {
  return latitude_e7 >= -kLatitudeLimitE7 && latitude_e7 <= kLatitudeLimitE7 &&
         longitude_e7 >= -kLongitudeLimitE7 && longitude_e7 <= kLongitudeLimitE7;
}

bool LocationMethodNeedsNetwork(LocationMethod method) {
  switch (method) {
    case LocationMethod::postal_code:
    case LocationMethod::city_address:
    case LocationMethod::network_estimate:
      return true;
    case LocationMethod::none:
    case LocationMethod::map_pin:
    case LocationMethod::coordinates:
      return false;
  }
  return false;
}

bool LocationMethodAvailable(LocationMethod method, bool network_connected,
                             bool basemap_available) {
  switch (method) {
    case LocationMethod::none:
      return false;
    // The pin needs something to drop it on. With the basemap embedded in
    // firmware this is true on a board with no card and no network.
    case LocationMethod::map_pin:
      return basemap_available;
    // Always possible, and the last resort when nothing else is.
    case LocationMethod::coordinates:
      return true;
    case LocationMethod::postal_code:
    case LocationMethod::city_address:
    case LocationMethod::network_estimate:
      return network_connected;
  }
  return false;
}

uint8_t AvailableLocationMethodCount(bool network_connected,
                                     bool basemap_available) {
  const LocationMethod all[] = {
      LocationMethod::map_pin,     LocationMethod::coordinates,
      LocationMethod::postal_code, LocationMethod::city_address,
      LocationMethod::network_estimate};
  uint8_t count = 0;
  for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
    if (LocationMethodAvailable(all[i], network_connected, basemap_available)) {
      ++count;
    }
  }
  return count;
}

uint16_t TierRadiusMiles(MapTier tier) {
  switch (tier) {
    case MapTier::local:
      return kLocalRadiusMiles;
    case MapTier::regional:
      return kRegionalRadiusMiles;
    case MapTier::travel:
      return kTravelRadiusMiles;
    case MapTier::none:
      return 0;
  }
  return 0;
}

uint16_t TierRadiusKm(MapTier tier) {
  const uint16_t miles = TierRadiusMiles(tier);
  if (miles == 0) return 0;
  return clamp_radius(static_cast<double>(miles) * kKmPerStatuteMile);
}

uint16_t radius_km_for_range_nm(uint16_t range_nm) {
  return clamp_radius(static_cast<double>(range_nm) * kKmPerNauticalMile *
                      kCoverageMargin);
}

const char* StepName(Step step) {
  switch (step) {
    case Step::welcome:
      return "Welcome";
    case Step::network:
      return "Wi-Fi";
    case Step::location:
      return "Location";
    case Step::maps:
      return "Maps";
    case Step::complete:
      return "Complete";
  }
  return "";
}

void Wizard::set_message(const char* text) {
  copy_text(state_.message, sizeof(state_.message), text);
}

void Wizard::begin(const Environment& environment) {
  state_ = State{};
  state_.network_connected = environment.network_connected;
  state_.basemap_available = environment.basemap_available;
  state_.sd_present = environment.sd_present;
  state_.radar_range_nm = environment.radar_range_nm;
  set_message("Let's set up your radio");
}

void Wizard::set_network_connected(bool connected) {
  state_.network_connected = connected;
  if (connected) state_.network_skipped = false;
}

void Wizard::set_radar_range_nm(uint16_t radar_range_nm) {
  state_.radar_range_nm = radar_range_nm;
  if (state_.coverage_evaluated) recommend();
}

bool Wizard::set_pin(int32_t latitude_e7, int32_t longitude_e7) {
  if (!coordinates_valid(latitude_e7, longitude_e7)) {
    set_message("Pin is off the map");
    return false;
  }
  // A pin at exactly 0,0 is the viewport's own starting centre, so it means
  // "never moved" far more often than it means the Gulf of Guinea. Typed
  // coordinates can still set it deliberately.
  if (latitude_e7 == 0 && longitude_e7 == 0) {
    set_message("Move the pin first");
    return false;
  }
  accept_location(&state_, LocationMethod::map_pin, latitude_e7, longitude_e7);
  set_message("Location set from map");
  return true;
}

bool Wizard::set_manual(int32_t latitude_e7, int32_t longitude_e7) {
  if (!coordinates_valid(latitude_e7, longitude_e7)) {
    set_message("Coordinates out of range");
    return false;
  }
  accept_location(&state_, LocationMethod::coordinates, latitude_e7,
                  longitude_e7);
  set_message("Coordinates accepted");
  return true;
}

bool Wizard::set_looked_up(LocationMethod method, int32_t latitude_e7,
                           int32_t longitude_e7) {
  // Guard the method as well as the coordinates: a lookup result arriving
  // while offline means the caller is confused about its own state, and
  // accepting it would record a provenance that cannot be true.
  if (!LocationMethodNeedsNetwork(method)) {
    set_message("Not a lookup method");
    return false;
  }
  if (!state_.network_connected) {
    set_message("Connect Wi-Fi to look up a location");
    return false;
  }
  if (!coordinates_valid(latitude_e7, longitude_e7)) {
    set_message("Lookup returned a bad position");
    return false;
  }
  accept_location(&state_, method, latitude_e7, longitude_e7);
  set_message("Location found");
  return true;
}

bool Wizard::select_tier(MapTier tier) {
  if (tier == MapTier::none) return false;
  state_.tier = tier;
  state_.maps_skipped = false;
  if (state_.coverage_evaluated) recommend();
  return true;
}

void Wizard::recommend() {
  Recommendation& plan = state_.recommendation;
  plan = Recommendation{};
  plan.min_zoom = kRecommendedMinZoom;
  plan.max_zoom = kRecommendedMaxZoom;
  // An explicitly chosen tier wins; otherwise size the pack to the radar
  // range, which is what the receiver will actually draw.
  plan.radius_km = state_.tier != MapTier::none
                       ? TierRadiusKm(state_.tier)
                       : radius_km_for_range_nm(state_.radar_range_nm);
  plan.needed = !state_.covered;
  copy_text(plan.name, sizeof(plan.name), "home");
}

void Wizard::evaluate_coverage(const PackCoverage* packs, size_t count) {
  state_.coverage_evaluated = true;
  state_.covered = false;
  state_.covering_pack[0] = '\0';
  if (!state_.location_valid) {
    recommend();
    set_message("Choose a location first");
    return;
  }

  if (packs != nullptr) {
    for (size_t i = 0; i < count; ++i) {
      const PackCoverage& pack = packs[i];
      if (!contains(pack, state_.latitude_e7, state_.longitude_e7)) continue;
      // Geographic containment is not coverage. A world basemap contains
      // every point and resolves none of them.
      if (pack.max_zoom < kMinDetailZoom) continue;
      state_.covered = true;
      copy_field(state_.covering_pack, sizeof(state_.covering_pack), pack.name,
                 sizeof(pack.name));
      break;
    }
  }

  recommend();
  set_message(state_.covered ? "Local map found for this location"
                             : "No local map yet: add one to continue offline");
}

Outcome Wizard::advance() {
  switch (state_.step) {
    case Step::welcome:
      state_.step = Step::network;
      set_message("Wi-Fi is optional; radio works without it");
      return Outcome::ok;

    case Step::network:
      state_.step = Step::location;
      if (AvailableLocationMethodCount(state_.network_connected,
                                       state_.basemap_available) == 0) {
        set_message("Enter coordinates, or skip for now");
      } else {
        set_message("Where will you usually use OrcSDR?");
      }
      return Outcome::ok;

    case Step::location:
      if (!state_.location_valid) {
        set_message("Set a location, or skip for now");
        return Outcome::blocked;
      }
      state_.step = Step::maps;
      set_message("Choose the map area to keep offline");
      // Persist the moment the location is accepted, not at the end: losing
      // power mid-setup should not discard it.
      return Outcome::needs_persist;

    case Step::maps:
      state_.step = Step::complete;
      set_message(state_.covered ? "Setup complete"
                                 : "Setup complete: map pending");
      return Outcome::finished;

    case Step::complete:
      return Outcome::finished;
  }
  return Outcome::blocked;
}

Outcome Wizard::skip() {
  switch (state_.step) {
    case Step::welcome:
      // Nothing to decline on the welcome screen; treat it as advancing.
      return advance();

    case Step::network:
      state_.network_skipped = true;
      state_.step = Step::location;
      set_message("Continuing offline");
      return Outcome::ok;

    case Step::location:
      state_.location_skipped = true;
      state_.step = Step::maps;
      set_message("Location not set; you can add it in settings");
      return Outcome::ok;

    case Step::maps:
      state_.maps_skipped = true;
      state_.step = Step::complete;
      set_message("No map area selected");
      return Outcome::finished;

    case Step::complete:
      return Outcome::finished;
  }
  return Outcome::blocked;
}

Outcome Wizard::quick_start() {
  // The welcome screen's second button. Everything is recorded as declined
  // rather than silently defaulted, so the app can tell a skipped step from
  // a completed one and re-offer it.
  state_.network_skipped = true;
  state_.location_skipped = true;
  state_.maps_skipped = true;
  state_.quick_started = true;
  state_.step = Step::complete;
  set_message("Skipped setup; you can finish it in settings");
  return Outcome::finished;
}

bool Wizard::back() {
  switch (state_.step) {
    case Step::welcome:
      return false;
    case Step::network:
      state_.step = Step::welcome;
      return true;
    case Step::location:
      state_.step = Step::network;
      return true;
    case Step::maps:
      state_.step = Step::location;
      return true;
    case Step::complete:
      // Finished setup is not a screen to reverse out of; settings owns
      // later changes.
      return false;
  }
  return false;
}

bool SetupRecordValid(const SetupRecord& record) {
  // A record written by a newer firmware may mean anything; refuse it rather
  // than interpret its fields under this version's assumptions.
  if (record.version != kSetupRecordVersion) return false;
  if (static_cast<uint8_t>(record.step) >= kStepCount) return false;
  if (record.location_valid &&
      !coordinates_valid(record.latitude_e7, record.longitude_e7)) {
    return false;
  }
  // A location cannot have arrived by no method, and a method cannot have
  // produced no location.
  if (record.location_valid == (record.method == LocationMethod::none)) {
    return false;
  }
  // Completion and step must agree. "Finished" that did not reach the last
  // step, or a wizard sitting on the last step but not marked finished, is a
  // record written by something confused; treat it as unusable.
  if (record.completed != (record.step == Step::complete)) return false;
  return true;
}

bool NeedsSetup(const SetupRecord& record) {
  if (!SetupRecordValid(record)) return true;
  return !record.completed;
}

SetupRecord Wizard::record() const {
  SetupRecord out;
  out.version = kSetupRecordVersion;
  out.completed = state_.step == Step::complete;
  out.step = state_.step;
  out.skips = static_cast<uint8_t>(
      (state_.network_skipped ? kSkipNetwork : 0) |
      (state_.location_skipped ? kSkipLocation : 0) |
      (state_.maps_skipped ? kSkipMaps : 0) |
      (state_.quick_started ? kSkipQuickStart : 0));
  out.method = state_.method;
  out.location_valid = state_.location_valid;
  out.latitude_e7 = state_.latitude_e7;
  out.longitude_e7 = state_.longitude_e7;
  return out;
}

bool Wizard::resume(const Environment& environment, const SetupRecord& saved) {
  // Start from a clean wizard in the CURRENT environment, then lay the stored
  // progress over it. A card inserted or a network joined since the last run
  // must be reflected, so nothing about availability comes from the record.
  begin(environment);
  if (!SetupRecordValid(saved)) return false;

  state_.step = saved.step;
  state_.network_skipped = (saved.skips & kSkipNetwork) != 0;
  state_.location_skipped = (saved.skips & kSkipLocation) != 0;
  state_.maps_skipped = (saved.skips & kSkipMaps) != 0;
  state_.quick_started = (saved.skips & kSkipQuickStart) != 0;
  state_.method = saved.method;
  state_.location_valid = saved.location_valid;
  state_.latitude_e7 = saved.latitude_e7;
  state_.longitude_e7 = saved.longitude_e7;

  // Coverage is deliberately NOT restored. Which packs are installed is a
  // property of the card right now, not of the last run, and a stale verdict
  // would claim a map that may have been removed.
  state_.coverage_evaluated = false;
  state_.covered = false;
  state_.covering_pack[0] = '\0';
  state_.recommendation = Recommendation{};

  set_message(saved.completed ? "Setup already complete"
                              : "Resuming setup where you left off");
  return true;
}

void Wizard::restart() {
  // Invoked from Settings. Keep whatever location is already known so the
  // picker can open there, but require setup to be finished again.
  const LocationMethod method = state_.method;
  const bool had_location = state_.location_valid;
  const int32_t lat = state_.latitude_e7;
  const int32_t lon = state_.longitude_e7;
  const bool network = state_.network_connected;
  const bool basemap = state_.basemap_available;
  const bool sd = state_.sd_present;
  const uint16_t range = state_.radar_range_nm;

  state_ = State{};
  state_.network_connected = network;
  state_.basemap_available = basemap;
  state_.sd_present = sd;
  state_.radar_range_nm = range;
  state_.method = method;
  state_.location_valid = had_location;
  state_.latitude_e7 = lat;
  state_.longitude_e7 = lon;
  set_message("Setup restarted");
}

size_t Wizard::provision_command(char* out, size_t size,
                                 const char* source_manifest) const {
  const Recommendation& plan = state_.recommendation;
  const char* manifest = (source_manifest != nullptr && source_manifest[0] != '\0')
                             ? source_manifest
                             : "<source>.manifest.json";
  const uint16_t radius = plan.radius_km != 0
                              ? plan.radius_km
                              : radius_km_for_range_nm(state_.radar_range_nm);
  // Shown on the device so the coordinates are not transcribed by hand on
  // the way to a PC. Seven decimals is the e7 convention's full precision.
  const int written = std::snprintf(
      out, size,
      "python tools/pack-builder/provision_pack.py"
      " --source-manifest %s"
      " --lat %.7f --lon %.7f --radius-km %u"
      " --name %s --sd-root <card>"
      " --min-zoom %u --max-zoom %u"
      " --builder-commit $(git rev-parse HEAD)",
      manifest, static_cast<double>(state_.latitude_e7) / 1.0e7,
      static_cast<double>(state_.longitude_e7) / 1.0e7,
      static_cast<unsigned>(radius),
      plan.name[0] != '\0' ? plan.name : "home",
      static_cast<unsigned>(plan.min_zoom != 0 ? plan.min_zoom
                                               : kRecommendedMinZoom),
      static_cast<unsigned>(plan.max_zoom != 0 ? plan.max_zoom
                                               : kRecommendedMaxZoom));
  return written < 0 ? 0 : static_cast<size_t>(written);
}

bool Wizard::self_check() {
  // The worst realistic first boot: freshly flashed from M5Burner, no card,
  // no network, but the basemap is in firmware.
  Environment blank;
  blank.network_connected = false;
  blank.basemap_available = true;
  blank.sd_present = false;
  blank.radar_range_nm = 25;

  Wizard wizard;
  wizard.begin(blank);
  if (wizard.state().step != Step::welcome) return false;

  // Offline, only the pin and typed coordinates are offered.
  if (AvailableLocationMethodCount(false, true) != 2) return false;
  if (LocationMethodAvailable(LocationMethod::postal_code, false, true)) {
    return false;
  }
  if (!LocationMethodAvailable(LocationMethod::map_pin, false, true)) {
    return false;
  }
  // With no basemap and no network, typed coordinates are the only way in.
  if (AvailableLocationMethodCount(false, false) != 1) return false;

  if (wizard.advance() != Outcome::ok) return false;  // -> network
  if (wizard.skip() != Outcome::ok) return false;     // decline Wi-Fi
  if (!wizard.state().network_skipped) return false;
  if (wizard.state().step != Step::location) return false;

  // A lookup must be refused while offline.
  if (wizard.set_looked_up(LocationMethod::postal_code, 440521000,
                           -1230867000)) {
    return false;
  }
  if (wizard.set_pin(0, 0)) return false;
  if (wizard.advance() != Outcome::blocked) return false;
  if (!wizard.set_pin(440521000, -1230867000)) return false;
  if (wizard.advance() != Outcome::needs_persist) return false;

  if (!wizard.select_tier(MapTier::regional)) return false;
  if (wizard.advance() != Outcome::finished) return false;
  if (!wizard.complete()) return false;

  // Quick start must reach the app from the welcome screen in one action.
  Wizard quick;
  quick.begin(blank);
  if (quick.quick_start() != Outcome::finished) return false;
  if (!quick.complete() || !quick.state().quick_started) return false;
  if (!quick.state().location_skipped) return false;

  if (radius_km_for_range_nm(0) != kMinRadiusKm) return false;
  if (TierRadiusKm(MapTier::local) != 81) return false;
  if (!coordinates_valid(0, 0)) return false;
  if (coordinates_valid(kLatitudeLimitE7 + 1, 0)) return false;

  char command[256]{};
  Wizard commander;
  commander.begin(blank);
  commander.advance();
  commander.skip();
  if (!commander.set_pin(440521000, -1230867000)) return false;
  commander.evaluate_coverage(nullptr, 0);
  if (commander.provision_command(command, sizeof(command),
                                  "oregon.manifest.json") == 0) {
    return false;
  }
  return std::strstr(command, "--lat 44.0521000") != nullptr;
}

}  // namespace orcsdr::setup_wizard
