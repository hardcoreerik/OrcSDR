// Host tests for the offline first-run setup wizard.
//
// The case that governs the design is the worst realistic first boot: a board
// 1-click flashed from M5Burner, with no SD card, no Wi-Fi credentials and no
// PC. Most of what follows checks that this state is fully navigable, and
// that nothing quietly gives a user a receiver which believes it is somewhere
// it is not.

#include "setup_wizard.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int g_failures = 0;

void check(bool condition, const char* expression, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    ++g_failures;
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

using orcsdr::setup_wizard::Environment;
using orcsdr::setup_wizard::LocationMethod;
using orcsdr::setup_wizard::MapTier;
using orcsdr::setup_wizard::Outcome;
using orcsdr::setup_wizard::PackCoverage;
using orcsdr::setup_wizard::Step;
using orcsdr::setup_wizard::Wizard;

// Eugene, Oregon -- the location used throughout the OrcMaps pack evidence.
constexpr int32_t kEugeneLatE7 = 440521000;
constexpr int32_t kEugeneLonE7 = -1230867000;

// Freshly flashed from M5Burner: basemap in firmware, nothing else.
Environment BlankDevice() {
  Environment environment;
  environment.network_connected = false;
  environment.basemap_available = true;
  environment.sd_present = false;
  environment.radar_range_nm = 25;
  return environment;
}

Environment ConnectedDevice() {
  Environment environment = BlankDevice();
  environment.network_connected = true;
  environment.sd_present = true;
  return environment;
}

PackCoverage make_pack(const char* name, double min_lat, double min_lon,
                       double max_lat, double max_lon, uint8_t min_zoom,
                       uint8_t max_zoom) {
  PackCoverage pack{};
  std::snprintf(pack.name, sizeof(pack.name), "%s", name);
  pack.min_lat_e7 = static_cast<int32_t>(min_lat * 1.0e7);
  pack.min_lon_e7 = static_cast<int32_t>(min_lon * 1.0e7);
  pack.max_lat_e7 = static_cast<int32_t>(max_lat * 1.0e7);
  pack.max_lon_e7 = static_cast<int32_t>(max_lon * 1.0e7);
  pack.min_zoom = min_zoom;
  pack.max_zoom = max_zoom;
  return pack;
}

PackCoverage world_basemap() {
  return make_pack("world-basemap", -85.0, -180.0, 85.0, 180.0, 1, 6);
}

PackCoverage eugene_detail() {
  return make_pack("home", 43.53108, -123.81811, 44.57312, -122.35529, 1, 13);
}

// Walks a blank device to the maps step with a pin dropped, as the UI would.
Wizard at_maps_step() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();  // -> network
  wizard.skip();     // decline Wi-Fi
  wizard.set_pin(kEugeneLatE7, kEugeneLonE7);
  wizard.advance();  // -> maps
  return wizard;
}

void test_self_check() { CHECK(Wizard::self_check()); }

// ---------------------------------------------------------------- first boot

void test_blank_device_can_finish_setup_offline() {
  // The headline requirement: flashed board, no card, no network, no PC.
  Wizard wizard;
  wizard.begin(BlankDevice());
  CHECK(wizard.state().step == Step::welcome);
  CHECK(wizard.advance() == Outcome::ok);
  CHECK(wizard.state().step == Step::network);

  CHECK(wizard.skip() == Outcome::ok);
  CHECK(wizard.state().network_skipped);
  CHECK(!wizard.state().network_connected);
  CHECK(wizard.state().step == Step::location);

  CHECK(wizard.set_pin(kEugeneLatE7, kEugeneLonE7));
  CHECK(wizard.state().method == LocationMethod::map_pin);
  CHECK(wizard.advance() == Outcome::needs_persist);
  CHECK(wizard.state().step == Step::maps);

  wizard.evaluate_coverage(nullptr, 0);
  CHECK(wizard.advance() == Outcome::finished);
  CHECK(wizard.complete());
  // Offline with no packs, the location is still set -- only the map is
  // outstanding.
  CHECK(wizard.state().location_valid);
  CHECK(wizard.state().recommendation.needed);
}

void test_network_is_never_required() {
  // Every step must be reachable and leaveable with the network declined.
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  CHECK(wizard.skip() == Outcome::ok);
  CHECK(wizard.state().step == Step::location);
  CHECK(wizard.skip() == Outcome::ok);
  CHECK(wizard.state().step == Step::maps);
  CHECK(wizard.skip() == Outcome::finished);
  CHECK(wizard.complete());
  CHECK(wizard.state().network_skipped);
  CHECK(wizard.state().location_skipped);
  CHECK(wizard.state().maps_skipped);
}

void test_quick_start_reaches_the_app_in_one_action() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  CHECK(wizard.quick_start() == Outcome::finished);
  CHECK(wizard.complete());
  CHECK(wizard.state().quick_started);
  // Declined, not silently defaulted: the app must be able to re-offer these.
  CHECK(wizard.state().network_skipped);
  CHECK(wizard.state().location_skipped);
  CHECK(wizard.state().maps_skipped);
  CHECK(!wizard.state().location_valid);
}

void test_step_order_matches_the_intended_flow() {
  Wizard wizard;
  wizard.begin(ConnectedDevice());
  CHECK(wizard.state().step == Step::welcome);
  wizard.advance();
  CHECK(wizard.state().step == Step::network);
  wizard.advance();
  CHECK(wizard.state().step == Step::location);
  CHECK(wizard.set_pin(kEugeneLatE7, kEugeneLonE7));
  wizard.advance();
  CHECK(wizard.state().step == Step::maps);
  wizard.evaluate_coverage(nullptr, 0);
  wizard.advance();
  CHECK(wizard.state().step == Step::complete);
  CHECK(std::strcmp(orcsdr::setup_wizard::StepName(Step::network), "Wi-Fi") == 0);
}

// ------------------------------------------------------- method availability

void test_offline_only_offers_the_two_methods_that_work() {
  using orcsdr::setup_wizard::AvailableLocationMethodCount;
  using orcsdr::setup_wizard::LocationMethodAvailable;

  // A postal-code or city lookup goes through Nominatim and the "use my
  // location" estimate through ipwho.is. None can be offered offline, and
  // there is no GNSS receiver to fall back to.
  CHECK(!LocationMethodAvailable(LocationMethod::postal_code, false, true));
  CHECK(!LocationMethodAvailable(LocationMethod::city_address, false, true));
  CHECK(!LocationMethodAvailable(LocationMethod::network_estimate, false, true));
  CHECK(LocationMethodAvailable(LocationMethod::map_pin, false, true));
  CHECK(LocationMethodAvailable(LocationMethod::coordinates, false, true));
  CHECK(AvailableLocationMethodCount(false, true) == 2);

  // Connected, everything is on the table.
  CHECK(AvailableLocationMethodCount(true, true) == 5);
}

void test_without_a_basemap_coordinates_are_the_only_offline_route() {
  using orcsdr::setup_wizard::AvailableLocationMethodCount;
  using orcsdr::setup_wizard::LocationMethodAvailable;
  CHECK(!LocationMethodAvailable(LocationMethod::map_pin, false, false));
  CHECK(LocationMethodAvailable(LocationMethod::coordinates, false, false));
  CHECK(AvailableLocationMethodCount(false, false) == 1);

  // And setup must still complete that way, or a missing basemap bricks the
  // first boot.
  Environment environment = BlankDevice();
  environment.basemap_available = false;
  Wizard wizard;
  wizard.begin(environment);
  wizard.advance();
  wizard.skip();
  CHECK(wizard.set_manual(kEugeneLatE7, kEugeneLonE7));
  CHECK(wizard.advance() == Outcome::needs_persist);
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(wizard.advance() == Outcome::finished);
}

void test_network_methods_classified_correctly() {
  using orcsdr::setup_wizard::LocationMethodNeedsNetwork;
  CHECK(LocationMethodNeedsNetwork(LocationMethod::postal_code));
  CHECK(LocationMethodNeedsNetwork(LocationMethod::city_address));
  CHECK(LocationMethodNeedsNetwork(LocationMethod::network_estimate));
  CHECK(!LocationMethodNeedsNetwork(LocationMethod::map_pin));
  CHECK(!LocationMethodNeedsNetwork(LocationMethod::coordinates));
}

void test_lookup_is_refused_while_offline() {
  // The failure this prevents: a stale lookup result being written as the
  // receiver's position when the network was never up.
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();
  CHECK(!wizard.set_looked_up(LocationMethod::postal_code, kEugeneLatE7,
                              kEugeneLonE7));
  CHECK(!wizard.state().location_valid);

  wizard.set_network_connected(true);
  CHECK(wizard.set_looked_up(LocationMethod::postal_code, kEugeneLatE7,
                             kEugeneLonE7));
  CHECK(wizard.state().method == LocationMethod::postal_code);
}

void test_lookup_rejects_a_non_lookup_method() {
  Wizard wizard;
  wizard.begin(ConnectedDevice());
  wizard.advance();
  wizard.advance();
  CHECK(!wizard.set_looked_up(LocationMethod::map_pin, kEugeneLatE7,
                              kEugeneLonE7));
  CHECK(!wizard.set_looked_up(LocationMethod::none, kEugeneLatE7, kEugeneLonE7));
}

void test_connecting_clears_the_skip_record() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();
  CHECK(wizard.state().network_skipped);
  wizard.set_network_connected(true);
  CHECK(!wizard.state().network_skipped);
  CHECK(wizard.state().network_connected);
}

// ------------------------------------------------------- location validation

void test_unmoved_pin_is_rejected() {
  // 0,0 is the viewport's own starting centre. Accepting it would hand a user
  // a receiver that believes it sits in the Gulf of Guinea.
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();
  CHECK(!wizard.set_pin(0, 0));
  CHECK(!wizard.state().location_valid);
  CHECK(wizard.state().method == LocationMethod::none);
  // Typed coordinates may still set it deliberately.
  CHECK(wizard.set_manual(0, 0));
  CHECK(wizard.state().method == LocationMethod::coordinates);
}

void test_out_of_range_coordinates_are_rejected() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();
  CHECK(!wizard.set_pin(orcsdr::setup_wizard::kLatitudeLimitE7 + 1, 0));
  CHECK(!wizard.set_pin(0, orcsdr::setup_wizard::kLongitudeLimitE7 + 1));
  CHECK(!wizard.set_manual(-orcsdr::setup_wizard::kLatitudeLimitE7 - 1, 0));
  CHECK(!wizard.state().location_valid);
  // The exact limits are valid: the poles and the antimeridian are places.
  CHECK(wizard.set_pin(orcsdr::setup_wizard::kLatitudeLimitE7,
                       orcsdr::setup_wizard::kLongitudeLimitE7));
}

void test_location_step_blocks_advance_but_allows_skip() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();
  CHECK(wizard.advance() == Outcome::blocked);
  CHECK(wizard.state().step == Step::location);
  CHECK(wizard.skip() == Outcome::ok);
  CHECK(wizard.state().step == Step::maps);
}

void test_back_navigation_preserves_the_location() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  CHECK(!wizard.back());  // nothing before welcome
  wizard.advance();
  wizard.skip();
  CHECK(wizard.set_pin(kEugeneLatE7, kEugeneLonE7));
  wizard.advance();
  CHECK(wizard.state().step == Step::maps);
  CHECK(wizard.back());
  CHECK(wizard.state().step == Step::location);
  CHECK(wizard.state().location_valid);
  CHECK(wizard.state().latitude_e7 == kEugeneLatE7);
  CHECK(wizard.back());
  CHECK(wizard.state().step == Step::network);
  CHECK(wizard.back());
  CHECK(wizard.state().step == Step::welcome);
}

void test_completion_is_terminal() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(wizard.advance() == Outcome::finished);
  CHECK(wizard.advance() == Outcome::finished);
  CHECK(wizard.skip() == Outcome::finished);
  CHECK(!wizard.back());
}

// ------------------------------------------------------------------ coverage

void test_world_basemap_alone_is_not_coverage() {
  // THE central rule. The firmware basemap contains every point on Earth.
  // Counting it as coverage would report a user's map as fine while it
  // cannot draw the town they are sitting in.
  Wizard wizard = at_maps_step();
  const PackCoverage packs[] = {world_basemap()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().recommendation.needed);
  CHECK(wizard.state().covering_pack[0] == '\0');
}

void test_detail_pack_containing_the_pin_is_coverage() {
  Wizard wizard = at_maps_step();
  const PackCoverage packs[] = {world_basemap(), eugene_detail()};
  wizard.evaluate_coverage(packs, 2);
  CHECK(wizard.state().covered);
  CHECK(!wizard.state().recommendation.needed);
  CHECK(std::strcmp(wizard.state().covering_pack, "home") == 0);
}

void test_detail_pack_elsewhere_is_not_coverage() {
  Wizard wizard = at_maps_step();
  const PackCoverage packs[] = {
      make_pack("portland", 45.3, -123.0, 45.7, -122.4, 1, 13)};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);
}

void test_detail_zoom_threshold_is_exact() {
  const uint8_t limit = orcsdr::setup_wizard::kMinDetailZoom;
  Wizard below = at_maps_step();
  const PackCoverage too_coarse[] = {
      make_pack("coarse", 43.0, -124.0, 45.0, -122.0, 1,
                static_cast<uint8_t>(limit - 1))};
  below.evaluate_coverage(too_coarse, 1);
  CHECK(!below.state().covered);

  Wizard at = at_maps_step();
  const PackCoverage exact[] = {
      make_pack("exact", 43.0, -124.0, 45.0, -122.0, 1, limit)};
  at.evaluate_coverage(exact, 1);
  CHECK(at.state().covered);
}

void test_pin_on_the_pack_boundary_counts_as_inside() {
  const PackCoverage pack = eugene_detail();
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();
  CHECK(wizard.set_pin(pack.min_lat_e7, pack.min_lon_e7));
  wizard.advance();
  const PackCoverage packs[] = {pack};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);
}

void test_moving_the_pin_invalidates_the_verdict() {
  // A stale "covered" verdict after the pin moves is how a user ends up
  // trusting a map that does not contain their new location.
  Wizard wizard = at_maps_step();
  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);

  CHECK(wizard.set_pin(455050000, -1226750000));  // Portland
  CHECK(!wizard.state().coverage_evaluated);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().covering_pack[0] == '\0');
  CHECK(!wizard.state().recommendation.needed);  // unknown, not "not needed"
}

void test_coverage_without_a_location_does_not_claim_success() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().recommendation.needed);
}

void test_unterminated_pack_name_output_is_bounded() {
  // The name field arrives from a manifest on a removable card and may use
  // every byte with no terminator.
  //
  // Scope, stated honestly: this pins the OUTPUT contract -- terminated, no
  // longer than the field. It does NOT prove the bounded read in copy_field
  // is required: reading past name[] lands in the struct's own following
  // members, so it stays inside the allocation and ASan cannot see it
  // (verified -- ASan passes either way). copy_field is kept because the
  // unbounded form interprets adjacent coordinate bytes as text.
  Wizard wizard = at_maps_step();
  PackCoverage pack = eugene_detail();
  std::memset(pack.name, 'a', sizeof(pack.name));
  const PackCoverage packs[] = {pack};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);
  CHECK(wizard.state().covering_pack[orcsdr::setup_wizard::kPackNameSize - 1] ==
        '\0');
  CHECK(std::strlen(wizard.state().covering_pack) ==
        orcsdr::setup_wizard::kPackNameSize - 1);
}

// ------------------------------------------------------------ recommendation

void test_recommended_radius_covers_the_radar_range() {
  using orcsdr::setup_wizard::radius_km_for_range_nm;
  const uint16_t ranges[] = {25, 50, 100, 150, 250};
  for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
    const uint16_t km = radius_km_for_range_nm(ranges[i]);
    // The pack must reach past the ring OrcSDR draws, never stop short.
    CHECK(static_cast<double>(km) >= static_cast<double>(ranges[i]) * 1.852);
  }
  CHECK(radius_km_for_range_nm(0) == 25);
  CHECK(radius_km_for_range_nm(65535) == 800);
  CHECK(radius_km_for_range_nm(25) <= radius_km_for_range_nm(50));
}

void test_tier_radii_convert_from_statute_miles() {
  using orcsdr::setup_wizard::TierRadiusKm;
  using orcsdr::setup_wizard::TierRadiusMiles;
  CHECK(TierRadiusMiles(MapTier::local) == 50);
  CHECK(TierRadiusMiles(MapTier::regional) == 150);
  CHECK(TierRadiusMiles(MapTier::travel) == 300);
  CHECK(TierRadiusMiles(MapTier::none) == 0);
  // 50 mi = 80.47 km, rounded up.
  CHECK(TierRadiusKm(MapTier::local) == 81);
  CHECK(TierRadiusKm(MapTier::regional) == 242);
  // The 300 mile tier is 483 km, so the ceiling must not clip it.
  CHECK(TierRadiusKm(MapTier::travel) == 483);
  CHECK(TierRadiusKm(MapTier::none) == 0);
}

void test_selected_tier_overrides_the_range_derived_radius() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  const uint16_t from_range = wizard.state().recommendation.radius_km;
  CHECK(wizard.select_tier(MapTier::travel));
  CHECK(wizard.state().recommendation.radius_km >= from_range);
  CHECK(wizard.state().recommendation.radius_km ==
        orcsdr::setup_wizard::TierRadiusKm(MapTier::travel));
  CHECK(!wizard.state().maps_skipped);
  CHECK(!wizard.select_tier(MapTier::none));
}

void test_radar_range_change_updates_the_recommendation() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  const uint16_t first = wizard.state().recommendation.radius_km;
  wizard.set_radar_range_nm(150);
  CHECK(wizard.state().recommendation.radius_km > first);
}

void test_recommendation_asks_for_the_established_zoom_range() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(wizard.state().recommendation.min_zoom ==
        orcsdr::setup_wizard::kRecommendedMinZoom);
  CHECK(wizard.state().recommendation.max_zoom ==
        orcsdr::setup_wizard::kRecommendedMaxZoom);
  CHECK(wizard.state().recommendation.max_zoom >=
        orcsdr::setup_wizard::kMinDetailZoom);
}

void test_provision_command_carries_the_exact_coordinates() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  char command[320]{};
  const size_t written =
      wizard.provision_command(command, sizeof(command), "oregon.manifest.json");
  CHECK(written > 0);
  CHECK(written < sizeof(command));
  CHECK(std::strstr(command, "--source-manifest oregon.manifest.json") != nullptr);
  CHECK(std::strstr(command, "--lat 44.0521000") != nullptr);
  CHECK(std::strstr(command, "--lon -123.0867000") != nullptr);
  CHECK(std::strstr(command, "--min-zoom 1") != nullptr);
  CHECK(std::strstr(command, "--max-zoom 13") != nullptr);
}

void test_provision_command_is_usable_before_coverage_is_evaluated() {
  // The recommendation is empty until evaluate_coverage runs; the command
  // must still name a sane radius and zoom range rather than zeroes.
  Wizard wizard = at_maps_step();
  char command[320]{};
  CHECK(wizard.provision_command(command, sizeof(command), "s.manifest.json") > 0);
  CHECK(std::strstr(command, "--radius-km 0") == nullptr);
  CHECK(std::strstr(command, "--min-zoom 0") == nullptr);
  CHECK(std::strstr(command, "--max-zoom 0") == nullptr);
}

void test_provision_command_reports_truncation() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  char small[16]{};
  const size_t needed = wizard.provision_command(small, sizeof(small), "s.json");
  // snprintf semantics: the return is what WOULD have been written, so a
  // caller can tell the buffer was too small rather than shipping half a
  // command.
  CHECK(needed >= sizeof(small));
  CHECK(small[sizeof(small) - 1] == '\0');
}

void test_provision_command_without_a_source_is_still_actionable() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  char command[320]{};
  CHECK(wizard.provision_command(command, sizeof(command), nullptr) > 0);
  CHECK(std::strstr(command, "manifest.json") != nullptr);
  CHECK(wizard.provision_command(command, sizeof(command), "") > 0);
  CHECK(std::strstr(command, "manifest.json") != nullptr);
}

// ------------------------------------------- interrupted setup / persistence

void test_blank_device_needs_setup() {
  using orcsdr::setup_wizard::NeedsSetup;
  using orcsdr::setup_wizard::SetupRecord;
  // Nothing stored yet: the default record must send a new device into setup.
  SetupRecord blank;
  CHECK(NeedsSetup(blank));
}

void test_completed_setup_does_not_re_enter() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  wizard.advance();
  CHECK(wizard.complete());
  const auto saved = wizard.record();
  CHECK(saved.completed);
  CHECK(saved.step == Step::complete);
  CHECK(!orcsdr::setup_wizard::NeedsSetup(saved));
}

void test_interrupted_setup_resumes_where_it_stopped() {
  // Power lost after choosing a location but before finishing. The location
  // must survive and the wizard must not think it completed.
  Wizard first;
  first.begin(BlankDevice());
  first.advance();
  first.skip();
  CHECK(first.set_pin(kEugeneLatE7, kEugeneLonE7));
  first.advance();  // -> maps, which is where persistence happens
  const auto saved = first.record();
  CHECK(!saved.completed);
  CHECK(saved.step == Step::maps);
  CHECK(saved.location_valid);
  CHECK(orcsdr::setup_wizard::NeedsSetup(saved));

  Wizard second;
  CHECK(second.resume(BlankDevice(), saved));
  CHECK(second.state().step == Step::maps);
  CHECK(second.state().location_valid);
  CHECK(second.state().latitude_e7 == kEugeneLatE7);
  CHECK(second.state().longitude_e7 == kEugeneLonE7);
  CHECK(second.state().method == LocationMethod::map_pin);
  CHECK(second.state().network_skipped);
  // And it can be finished from there.
  second.evaluate_coverage(nullptr, 0);
  CHECK(second.advance() == Outcome::finished);
  CHECK(second.record().completed);
}

void test_resume_resamples_the_environment() {
  // A card inserted or Wi-Fi joined since the interrupted run must be
  // reflected; availability must never come from the stored record.
  Wizard first;
  first.begin(BlankDevice());
  first.advance();
  first.skip();
  first.set_pin(kEugeneLatE7, kEugeneLonE7);
  first.advance();
  const auto saved = first.record();

  Wizard second;
  CHECK(second.resume(ConnectedDevice(), saved));
  CHECK(second.state().network_connected);
  CHECK(second.state().sd_present);
  // The stored skip record is still honoured even though Wi-Fi is now up.
  CHECK(second.state().network_skipped);
}

void test_resume_does_not_restore_a_stale_coverage_verdict() {
  // Which packs are installed is a property of the card right now. A restored
  // "covered" verdict would claim a map that may have been removed.
  Wizard first = at_maps_step();
  const PackCoverage packs[] = {eugene_detail()};
  first.evaluate_coverage(packs, 1);
  CHECK(first.state().covered);
  const auto saved = first.record();

  Wizard second;
  CHECK(second.resume(BlankDevice(), saved));
  CHECK(!second.state().coverage_evaluated);
  CHECK(!second.state().covered);
  CHECK(second.state().covering_pack[0] == 0);
}

void test_unusable_records_are_rejected() {
  using orcsdr::setup_wizard::NeedsSetup;
  using orcsdr::setup_wizard::SetupRecord;
  using orcsdr::setup_wizard::SetupRecordValid;

  Wizard done = at_maps_step();
  done.evaluate_coverage(nullptr, 0);
  done.advance();
  const auto good = done.record();
  CHECK(SetupRecordValid(good));

  // Written by a newer firmware: refuse rather than reinterpret.
  SetupRecord future = good;
  future.version = static_cast<uint8_t>(good.version + 1);
  CHECK(!SetupRecordValid(future));
  CHECK(NeedsSetup(future));

  // Claims completion without reaching the last step.
  SetupRecord lying = good;
  lying.step = Step::location;
  CHECK(!SetupRecordValid(lying));

  // A location that arrived by no method, and a method with no location.
  SetupRecord orphan = good;
  orphan.method = LocationMethod::none;
  CHECK(!SetupRecordValid(orphan));
  SetupRecord methodless = good;
  methodless.location_valid = false;
  CHECK(!SetupRecordValid(methodless));

  // Out-of-range coordinates.
  SetupRecord impossible = good;
  impossible.latitude_e7 = orcsdr::setup_wizard::kLatitudeLimitE7 + 1;
  CHECK(!SetupRecordValid(impossible));

  // A step value no firmware ever wrote.
  SetupRecord garbage = good;
  garbage.step = static_cast<Step>(200);
  CHECK(!SetupRecordValid(garbage));

  // A rejected record must leave a usable, fresh wizard.
  Wizard wizard;
  CHECK(!wizard.resume(BlankDevice(), future));
  CHECK(wizard.state().step == Step::welcome);
  CHECK(!wizard.state().location_valid);
}

void test_skip_flags_round_trip() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.advance();
  wizard.skip();  // network
  wizard.skip();  // location
  wizard.skip();  // maps -> complete
  CHECK(wizard.complete());
  const auto saved = wizard.record();
  CHECK((saved.skips & orcsdr::setup_wizard::kSkipNetwork) != 0);
  CHECK((saved.skips & orcsdr::setup_wizard::kSkipLocation) != 0);
  CHECK((saved.skips & orcsdr::setup_wizard::kSkipMaps) != 0);

  Wizard restored;
  CHECK(restored.resume(BlankDevice(), saved));
  CHECK(restored.state().network_skipped);
  CHECK(restored.state().location_skipped);
  CHECK(restored.state().maps_skipped);
}

void test_quick_start_round_trips() {
  Wizard wizard;
  wizard.begin(BlankDevice());
  wizard.quick_start();
  const auto saved = wizard.record();
  CHECK(saved.completed);
  CHECK((saved.skips & orcsdr::setup_wizard::kSkipQuickStart) != 0);
  CHECK(!orcsdr::setup_wizard::NeedsSetup(saved));

  Wizard restored;
  CHECK(restored.resume(BlankDevice(), saved));
  CHECK(restored.state().quick_started);
}

// ------------------------------------------------- re-running from Settings

void test_restart_reopens_setup_without_losing_the_location() {
  // Invoked from Settings. Re-opening the wizard and backing out of it must
  // never destroy a working configuration.
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  wizard.advance();
  CHECK(wizard.complete());

  wizard.restart();
  CHECK(wizard.state().step == Step::welcome);
  CHECK(!wizard.complete());
  CHECK(orcsdr::setup_wizard::NeedsSetup(wizard.record()));
  // The known location is kept so the picker can open there.
  CHECK(wizard.state().location_valid);
  CHECK(wizard.state().latitude_e7 == kEugeneLatE7);
  CHECK(wizard.state().method == LocationMethod::map_pin);
  // Progress flags are cleared.
  CHECK(!wizard.state().network_skipped);
  CHECK(!wizard.state().maps_skipped);
  CHECK(!wizard.state().quick_started);
}

void test_restart_then_abandon_keeps_the_old_location() {
  Wizard wizard = at_maps_step();
  wizard.evaluate_coverage(nullptr, 0);
  wizard.advance();
  const int32_t original_lat = wizard.state().latitude_e7;

  wizard.restart();
  // User immediately skips everything rather than choosing again.
  wizard.advance();
  wizard.skip();
  wizard.skip();
  wizard.skip();
  CHECK(wizard.complete());
  CHECK(wizard.state().location_valid);
  CHECK(wizard.state().latitude_e7 == original_lat);
  CHECK(wizard.record().location_valid);
}

void test_restart_preserves_the_environment() {
  Wizard wizard;
  wizard.begin(ConnectedDevice());
  wizard.advance();
  wizard.advance();
  wizard.set_pin(kEugeneLatE7, kEugeneLonE7);
  wizard.restart();
  CHECK(wizard.state().network_connected);
  CHECK(wizard.state().sd_present);
  CHECK(wizard.state().basemap_available);
}

// ----------------------------------------------------------------- lifecycle

void test_begin_resets_previous_state() {
  Wizard wizard = at_maps_step();
  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);

  Environment environment = ConnectedDevice();
  environment.radar_range_nm = 100;
  wizard.begin(environment);
  CHECK(wizard.state().step == Step::welcome);
  CHECK(!wizard.state().location_valid);
  CHECK(!wizard.state().covered);
  CHECK(!wizard.state().coverage_evaluated);
  CHECK(!wizard.state().network_skipped);
  CHECK(!wizard.state().quick_started);
  CHECK(wizard.state().method == LocationMethod::none);
  CHECK(wizard.state().tier == MapTier::none);
  CHECK(wizard.state().latitude_e7 == 0);
  CHECK(wizard.state().radar_range_nm == 100);
  CHECK(wizard.state().network_connected);
  CHECK(wizard.state().sd_present);
}

void test_messages_are_always_terminated() {
  const auto terminated = [](const char* text, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (text[i] == '\0') return true;
    }
    return false;
  };
  Wizard wizard;
  wizard.begin(BlankDevice());
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
  wizard.set_pin(0, 0);
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
  wizard.advance();
  wizard.skip();
  wizard.set_pin(kEugeneLatE7, kEugeneLonE7);
  wizard.advance();
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
  CHECK(terminated(wizard.state().recommendation.name,
                   orcsdr::setup_wizard::kPackNameSize));
  wizard.advance();
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
}

}  // namespace

int main() {
  test_self_check();

  test_blank_device_can_finish_setup_offline();
  test_network_is_never_required();
  test_quick_start_reaches_the_app_in_one_action();
  test_step_order_matches_the_intended_flow();

  test_offline_only_offers_the_two_methods_that_work();
  test_without_a_basemap_coordinates_are_the_only_offline_route();
  test_network_methods_classified_correctly();
  test_lookup_is_refused_while_offline();
  test_lookup_rejects_a_non_lookup_method();
  test_connecting_clears_the_skip_record();

  test_unmoved_pin_is_rejected();
  test_out_of_range_coordinates_are_rejected();
  test_location_step_blocks_advance_but_allows_skip();
  test_back_navigation_preserves_the_location();
  test_completion_is_terminal();

  test_world_basemap_alone_is_not_coverage();
  test_detail_pack_containing_the_pin_is_coverage();
  test_detail_pack_elsewhere_is_not_coverage();
  test_detail_zoom_threshold_is_exact();
  test_pin_on_the_pack_boundary_counts_as_inside();
  test_moving_the_pin_invalidates_the_verdict();
  test_coverage_without_a_location_does_not_claim_success();
  test_unterminated_pack_name_output_is_bounded();

  test_recommended_radius_covers_the_radar_range();
  test_tier_radii_convert_from_statute_miles();
  test_selected_tier_overrides_the_range_derived_radius();
  test_radar_range_change_updates_the_recommendation();
  test_recommendation_asks_for_the_established_zoom_range();
  test_provision_command_carries_the_exact_coordinates();
  test_provision_command_is_usable_before_coverage_is_evaluated();
  test_provision_command_reports_truncation();
  test_provision_command_without_a_source_is_still_actionable();

  test_blank_device_needs_setup();
  test_completed_setup_does_not_re_enter();
  test_interrupted_setup_resumes_where_it_stopped();
  test_resume_resamples_the_environment();
  test_resume_does_not_restore_a_stale_coverage_verdict();
  test_unusable_records_are_rejected();
  test_skip_flags_round_trip();
  test_quick_start_round_trips();
  test_restart_reopens_setup_without_losing_the_location();
  test_restart_then_abandon_keeps_the_old_location();
  test_restart_preserves_the_environment();

  test_begin_resets_previous_state();
  test_messages_are_always_terminated();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return EXIT_FAILURE;
  }
  std::puts("setup wizard tests passed");
  return EXIT_SUCCESS;
}
