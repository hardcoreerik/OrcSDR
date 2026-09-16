#include "setup_wizard.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::setup_wizard {
namespace {

// A pack should cover at least what the radar draws, plus a margin so the
// map does not end exactly at the edge of the range ring.
constexpr double kKmPerNauticalMile = 1.852;
constexpr double kCoverageMargin = 1.25;
constexpr uint16_t kMinRadiusKm = 25;
constexpr uint16_t kMaxRadiusKm = 400;

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
// A PackCoverage.name is filled in by the UI layer from a manifest on a
// removable card. If that copy ever fills the field without leaving room for
// a terminator, treating it as a C string reads past the end of the struct,
// so the read is bounded by the source's own size rather than trusting it.
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

}  // namespace

bool coordinates_valid(int32_t latitude_e7, int32_t longitude_e7) {
  return latitude_e7 >= -kLatitudeLimitE7 && latitude_e7 <= kLatitudeLimitE7 &&
         longitude_e7 >= -kLongitudeLimitE7 && longitude_e7 <= kLongitudeLimitE7;
}

uint16_t radius_km_for_range_nm(uint16_t range_nm) {
  const double km = static_cast<double>(range_nm) * kKmPerNauticalMile *
                    kCoverageMargin;
  if (km <= static_cast<double>(kMinRadiusKm)) return kMinRadiusKm;
  if (km >= static_cast<double>(kMaxRadiusKm)) return kMaxRadiusKm;
  // Round up: a pack that stops just short of the radar range is the one
  // failure this calculation exists to prevent.
  return static_cast<uint16_t>(km + 0.999);
}

void Wizard::begin(bool overview_available, uint16_t radar_range_nm) {
  state_ = State{};
  state_.overview_available = overview_available;
  state_.radar_range_nm = radar_range_nm;
  copy_text(state_.message, sizeof(state_.message),
            overview_available ? "Offline setup: drop a pin on the map"
                               : "Offline setup: enter your coordinates");
}

void Wizard::set_radar_range_nm(uint16_t radar_range_nm) {
  state_.radar_range_nm = radar_range_nm;
  if (state_.coverage_evaluated) recommend();
}

bool Wizard::set_pin(int32_t latitude_e7, int32_t longitude_e7) {
  if (!coordinates_valid(latitude_e7, longitude_e7)) {
    copy_text(state_.message, sizeof(state_.message), "Pin is off the map");
    return false;
  }
  // A pin at exactly 0,0 is the viewport's own starting centre, so it means
  // "never moved" far more often than it means the Gulf of Guinea. Manual
  // entry can still set it deliberately.
  if (latitude_e7 == 0 && longitude_e7 == 0) {
    copy_text(state_.message, sizeof(state_.message), "Move the pin first");
    return false;
  }
  state_.latitude_e7 = latitude_e7;
  state_.longitude_e7 = longitude_e7;
  state_.source = LocationSource::map_pin;
  state_.location_valid = true;
  // Any earlier verdict belongs to the old location.
  state_.coverage_evaluated = false;
  state_.covered = false;
  state_.covering_pack[0] = '\0';
  state_.recommendation = Recommendation{};
  copy_text(state_.message, sizeof(state_.message), "Pin set");
  return true;
}

bool Wizard::set_manual(int32_t latitude_e7, int32_t longitude_e7) {
  if (!coordinates_valid(latitude_e7, longitude_e7)) {
    copy_text(state_.message, sizeof(state_.message), "Coordinates out of range");
    return false;
  }
  state_.latitude_e7 = latitude_e7;
  state_.longitude_e7 = longitude_e7;
  state_.source = LocationSource::manual_entry;
  state_.location_valid = true;
  state_.coverage_evaluated = false;
  state_.covered = false;
  state_.covering_pack[0] = '\0';
  state_.recommendation = Recommendation{};
  copy_text(state_.message, sizeof(state_.message), "Coordinates accepted");
  return true;
}

void Wizard::recommend() {
  Recommendation& plan = state_.recommendation;
  plan = Recommendation{};
  plan.min_zoom = kRecommendedMinZoom;
  plan.max_zoom = kRecommendedMaxZoom;
  plan.radius_km = radius_km_for_range_nm(state_.radar_range_nm);
  plan.needed = !state_.covered;
  copy_text(plan.name, sizeof(plan.name), "home");
}

void Wizard::evaluate_coverage(const PackCoverage* packs, size_t count) {
  state_.coverage_evaluated = true;
  state_.covered = false;
  state_.covering_pack[0] = '\0';
  if (!state_.location_valid) {
    recommend();
    copy_text(state_.message, sizeof(state_.message), "Choose a location first");
    return;
  }

  if (packs != nullptr) {
    for (size_t i = 0; i < count; ++i) {
      const PackCoverage& pack = packs[i];
      if (!contains(pack, state_.latitude_e7, state_.longitude_e7)) continue;
      // Geographic containment is not enough. An overview pack contains
      // every point and resolves none of them.
      if (pack.max_zoom < kMinDetailZoom) continue;
      state_.covered = true;
      copy_field(state_.covering_pack, sizeof(state_.covering_pack), pack.name,
                 sizeof(pack.name));
      break;
    }
  }

  recommend();
  copy_text(state_.message, sizeof(state_.message),
            state_.covered ? "Local map found for this location"
                           : "No local map yet: generate one on a PC");
}

Outcome Wizard::advance() {
  switch (state_.step) {
    case Step::welcome:
      state_.step = Step::pick_location;
      copy_text(state_.message, sizeof(state_.message),
                state_.overview_available ? "Drag the map, then confirm"
                                          : "Enter latitude and longitude");
      return Outcome::ok;

    case Step::pick_location:
      if (!state_.location_valid) {
        copy_text(state_.message, sizeof(state_.message),
                  "Set a location to continue");
        return Outcome::blocked;
      }
      state_.step = Step::confirm_location;
      copy_text(state_.message, sizeof(state_.message), "Confirm this location");
      return Outcome::ok;

    case Step::confirm_location:
      if (!state_.location_valid) return Outcome::blocked;
      state_.step = Step::map_coverage;
      // The caller must supply the installed packs; until it does, the
      // coverage verdict is unknown rather than assumed absent.
      return Outcome::needs_persist;

    case Step::map_coverage:
      if (!state_.coverage_evaluated) {
        copy_text(state_.message, sizeof(state_.message),
                  "Checking installed map packs");
        return Outcome::blocked;
      }
      state_.step = Step::complete;
      copy_text(state_.message, sizeof(state_.message),
                state_.covered ? "Setup complete" : "Setup complete: map pending");
      return Outcome::finished;

    case Step::complete:
      return Outcome::finished;
  }
  return Outcome::blocked;
}

bool Wizard::back() {
  switch (state_.step) {
    case Step::welcome:
      return false;
    case Step::pick_location:
      state_.step = Step::welcome;
      return true;
    case Step::confirm_location:
      state_.step = Step::pick_location;
      return true;
    case Step::map_coverage:
      state_.step = Step::confirm_location;
      return true;
    case Step::complete:
      // Finished setup is not a screen to reverse out of; the settings app
      // owns later changes.
      return false;
  }
  return false;
}

size_t Wizard::provision_command(char* out, size_t size,
                                 const char* source_manifest) const {
  const Recommendation& plan = state_.recommendation;
  const char* manifest = (source_manifest != nullptr && source_manifest[0] != '\0')
                             ? source_manifest
                             : "<source>.manifest.json";
  // Printed on the device so the coordinates cannot be transcribed wrongly
  // on the way to the PC. Seven decimals is the e7 convention's full
  // precision.
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
      static_cast<unsigned>(plan.radius_km),
      plan.name[0] != '\0' ? plan.name : "home",
      static_cast<unsigned>(plan.min_zoom), static_cast<unsigned>(plan.max_zoom));
  return written < 0 ? 0 : static_cast<size_t>(written);
}

bool Wizard::self_check() {
  Wizard wizard;
  wizard.begin(true, 25);
  if (wizard.state().step != Step::welcome) return false;
  if (wizard.advance() != Outcome::ok) return false;

  // A pin that was never moved must not be accepted as a location.
  if (wizard.set_pin(0, 0)) return false;
  if (wizard.advance() != Outcome::blocked) return false;

  if (!wizard.set_pin(440521000, -1230867000)) return false;
  if (wizard.advance() != Outcome::ok) return false;
  if (wizard.advance() != Outcome::needs_persist) return false;

  // An overview-only card leaves the location uncovered.
  PackCoverage overview{};
  copy_text(overview.name, sizeof(overview.name), "world-overview");
  overview.min_lat_e7 = -850000000;
  overview.max_lat_e7 = 850000000;
  overview.min_lon_e7 = -1800000000;
  overview.max_lon_e7 = 1800000000;
  overview.min_zoom = 1;
  overview.max_zoom = 7;
  wizard.evaluate_coverage(&overview, 1);
  if (wizard.state().covered) return false;
  if (!wizard.state().recommendation.needed) return false;
  if (wizard.state().recommendation.radius_km < kMinRadiusKm) return false;

  if (wizard.advance() != Outcome::finished) return false;
  if (!wizard.complete()) return false;

  if (radius_km_for_range_nm(0) != kMinRadiusKm) return false;
  if (radius_km_for_range_nm(65535) != kMaxRadiusKm) return false;
  if (!coordinates_valid(0, 0)) return false;
  if (coordinates_valid(kLatitudeLimitE7 + 1, 0)) return false;

  char command[256]{};
  if (wizard.provision_command(command, sizeof(command), "oregon.manifest.json") == 0) {
    return false;
  }
  return std::strstr(command, "--lat 44.0521000") != nullptr;
}

}  // namespace orcsdr::setup_wizard
