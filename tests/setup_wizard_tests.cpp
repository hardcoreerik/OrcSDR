// Host tests for the offline first-run setup wizard.
//
// The wizard exists so a receiver can establish its own position with no
// network, so these tests care most about the cases that would quietly give a
// user a device that believes it is somewhere it is not, or a map that cannot
// resolve where it is.

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

using orcsdr::setup_wizard::LocationSource;
using orcsdr::setup_wizard::Outcome;
using orcsdr::setup_wizard::PackCoverage;
using orcsdr::setup_wizard::Step;
using orcsdr::setup_wizard::Wizard;

// Eugene, Oregon -- the location used throughout the OrcMaps pack evidence.
constexpr int32_t kEugeneLatE7 = 440521000;
constexpr int32_t kEugeneLonE7 = -1230867000;

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

PackCoverage world_overview() {
  return make_pack("world-overview", -85.0, -180.0, 85.0, 180.0, 1, 7);
}

PackCoverage eugene_detail() {
  return make_pack("home-eugene", 43.60294, -123.71644, 44.50126, -122.45696,
                   1, 13);
}

// Walks a wizard to the coverage step with a valid pin, as the UI would.
Wizard at_coverage_step() {
  Wizard wizard;
  wizard.begin(true, 25);
  wizard.advance();
  wizard.set_pin(kEugeneLatE7, kEugeneLonE7);
  wizard.advance();
  wizard.advance();
  return wizard;
}

void test_self_check() { CHECK(Wizard::self_check()); }

void test_step_order_and_gating() {
  Wizard wizard;
  wizard.begin(true, 25);
  CHECK(wizard.state().step == Step::welcome);
  CHECK(wizard.advance() == Outcome::ok);
  CHECK(wizard.state().step == Step::pick_location);

  // Without a location the wizard must not move on.
  CHECK(wizard.advance() == Outcome::blocked);
  CHECK(wizard.state().step == Step::pick_location);

  CHECK(wizard.set_pin(kEugeneLatE7, kEugeneLonE7));
  CHECK(wizard.advance() == Outcome::ok);
  CHECK(wizard.state().step == Step::confirm_location);

  // Confirming the location is the point at which it must be persisted.
  CHECK(wizard.advance() == Outcome::needs_persist);
  CHECK(wizard.state().step == Step::map_coverage);

  // Coverage has not been evaluated, so completing is still blocked: the
  // wizard must not claim a verdict it was never given the packs to reach.
  CHECK(wizard.advance() == Outcome::blocked);
  CHECK(wizard.state().step == Step::map_coverage);

  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.advance() == Outcome::finished);
  CHECK(wizard.complete());
  // Completion is terminal and idempotent.
  CHECK(wizard.advance() == Outcome::finished);
  CHECK(!wizard.back());
}

void test_back_navigation() {
  Wizard wizard;
  wizard.begin(true, 25);
  CHECK(!wizard.back());  // nothing before the welcome step
  wizard.advance();
  CHECK(wizard.back());
  CHECK(wizard.state().step == Step::welcome);

  wizard.advance();
  wizard.set_pin(kEugeneLatE7, kEugeneLonE7);
  wizard.advance();
  CHECK(wizard.state().step == Step::confirm_location);
  CHECK(wizard.back());
  CHECK(wizard.state().step == Step::pick_location);
  // Going back must not discard an accepted location.
  CHECK(wizard.state().location_valid);
  CHECK(wizard.state().latitude_e7 == kEugeneLatE7);
}

void test_unmoved_pin_is_rejected() {
  // 0,0 is the viewport's own starting centre. Accepting it would hand a
  // user a receiver that believes it sits in the Gulf of Guinea.
  Wizard wizard;
  wizard.begin(true, 25);
  wizard.advance();
  CHECK(!wizard.set_pin(0, 0));
  CHECK(!wizard.state().location_valid);
  CHECK(wizard.state().source == LocationSource::none);

  // Manual entry may still set it deliberately.
  CHECK(wizard.set_manual(0, 0));
  CHECK(wizard.state().location_valid);
  CHECK(wizard.state().source == LocationSource::manual_entry);
}

void test_out_of_range_coordinates_are_rejected() {
  Wizard wizard;
  wizard.begin(true, 25);
  wizard.advance();
  CHECK(!wizard.set_pin(orcsdr::setup_wizard::kLatitudeLimitE7 + 1, 0));
  CHECK(!wizard.set_pin(-orcsdr::setup_wizard::kLatitudeLimitE7 - 1, 0));
  CHECK(!wizard.set_pin(0, orcsdr::setup_wizard::kLongitudeLimitE7 + 1));
  CHECK(!wizard.set_manual(0, -orcsdr::setup_wizard::kLongitudeLimitE7 - 1));
  CHECK(!wizard.state().location_valid);

  // The exact limits are valid: the poles and the antimeridian are places.
  CHECK(wizard.set_pin(orcsdr::setup_wizard::kLatitudeLimitE7,
                       orcsdr::setup_wizard::kLongitudeLimitE7));
  CHECK(wizard.state().location_valid);
}

void test_source_is_recorded() {
  Wizard wizard;
  wizard.begin(true, 25);
  wizard.advance();
  CHECK(wizard.set_pin(kEugeneLatE7, kEugeneLonE7));
  CHECK(wizard.state().source == LocationSource::map_pin);
  CHECK(wizard.set_manual(kEugeneLatE7, kEugeneLonE7));
  CHECK(wizard.state().source == LocationSource::manual_entry);
}

void test_no_overview_routes_to_manual_entry() {
  // A card with no usable map must still let setup finish, or a missing file
  // becomes a bricked first boot.
  Wizard wizard;
  wizard.begin(false, 25);
  CHECK(!wizard.state().overview_available);
  CHECK(wizard.advance() == Outcome::ok);
  CHECK(wizard.set_manual(kEugeneLatE7, kEugeneLonE7));
  CHECK(wizard.advance() == Outcome::ok);
  CHECK(wizard.advance() == Outcome::needs_persist);
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(wizard.advance() == Outcome::finished);
  CHECK(wizard.complete());
}

void test_overview_pack_alone_is_not_coverage() {
  // THE central rule. An overview pack geographically contains every point
  // on Earth. Counting that as coverage would tell a user their map is fine
  // while it cannot resolve their own town.
  Wizard wizard = at_coverage_step();
  const PackCoverage packs[] = {world_overview()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().recommendation.needed);
  CHECK(wizard.state().covering_pack[0] == '\0');
}

void test_detail_pack_containing_the_pin_is_coverage() {
  Wizard wizard = at_coverage_step();
  const PackCoverage packs[] = {world_overview(), eugene_detail()};
  wizard.evaluate_coverage(packs, 2);
  CHECK(wizard.state().covered);
  CHECK(!wizard.state().recommendation.needed);
  CHECK(std::strcmp(wizard.state().covering_pack, "home-eugene") == 0);
}

void test_detail_pack_elsewhere_is_not_coverage() {
  // A detail pack for the wrong place must not satisfy the check.
  Wizard wizard = at_coverage_step();
  const PackCoverage packs[] = {
      make_pack("portland", 45.3, -123.0, 45.7, -122.4, 1, 13)};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().recommendation.needed);
}

void test_pack_just_below_detail_zoom_is_not_coverage() {
  Wizard wizard = at_coverage_step();
  const uint8_t below = orcsdr::setup_wizard::kMinDetailZoom - 1;
  const PackCoverage packs[] = {
      make_pack("coarse", 43.0, -124.0, 45.0, -122.0, 1, below)};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);

  Wizard exact = at_coverage_step();
  const PackCoverage at_limit[] = {
      make_pack("exact", 43.0, -124.0, 45.0, -122.0, 1,
                orcsdr::setup_wizard::kMinDetailZoom)};
  exact.evaluate_coverage(at_limit, 1);
  CHECK(exact.state().covered);
}

void test_pin_on_the_pack_boundary_counts_as_inside() {
  Wizard wizard;
  wizard.begin(true, 25);
  wizard.advance();
  const PackCoverage pack = eugene_detail();
  // A pin exactly on the southern edge is inside the pack's coverage.
  CHECK(wizard.set_pin(pack.min_lat_e7, pack.min_lon_e7));
  wizard.advance();
  wizard.advance();
  const PackCoverage packs[] = {pack};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);
}

void test_changing_the_location_invalidates_the_verdict() {
  // A stale "covered" verdict after the pin moves is how a user ends up
  // trusting a map that does not contain their new location.
  Wizard wizard = at_coverage_step();
  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);

  CHECK(wizard.set_pin(455000000, -1227000000));  // Portland
  CHECK(!wizard.state().coverage_evaluated);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().covering_pack[0] == '\0');
  CHECK(!wizard.state().recommendation.needed);  // unknown, not "not needed"
}

void test_coverage_without_a_location_does_not_claim_success() {
  Wizard wizard;
  wizard.begin(true, 25);
  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(!wizard.state().covered);
  CHECK(wizard.state().recommendation.needed);
}

void test_recommended_radius_covers_the_radar_range() {
  using orcsdr::setup_wizard::radius_km_for_range_nm;
  // The pack must reach past the range ring OrcSDR draws, never stop short.
  const uint16_t ranges[] = {25, 50, 100, 150};
  for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
    const uint16_t km = radius_km_for_range_nm(ranges[i]);
    const double range_km = static_cast<double>(ranges[i]) * 1.852;
    CHECK(static_cast<double>(km) >= range_km);
  }
  // Monotonic, and bounded at both ends so a nonsense range cannot ask for
  // a planet-sized pack.
  CHECK(radius_km_for_range_nm(25) <= radius_km_for_range_nm(50));
  CHECK(radius_km_for_range_nm(0) == 25);
  CHECK(radius_km_for_range_nm(65535) == 400);
}

void test_radar_range_change_updates_the_recommendation() {
  Wizard wizard = at_coverage_step();
  const PackCoverage packs[] = {world_overview()};
  wizard.evaluate_coverage(packs, 1);
  const uint16_t first = wizard.state().recommendation.radius_km;
  wizard.set_radar_range_nm(150);
  CHECK(wizard.state().recommendation.radius_km > first);
  CHECK(wizard.state().recommendation.needed);
}

void test_recommendation_asks_for_the_established_zoom_range() {
  Wizard wizard = at_coverage_step();
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(wizard.state().recommendation.min_zoom ==
        orcsdr::setup_wizard::kRecommendedMinZoom);
  CHECK(wizard.state().recommendation.max_zoom ==
        orcsdr::setup_wizard::kRecommendedMaxZoom);
  CHECK(wizard.state().recommendation.max_zoom >=
        orcsdr::setup_wizard::kMinDetailZoom);
}

void test_provision_command_carries_the_exact_coordinates() {
  // The command is shown on the device so coordinates are not transcribed by
  // hand on the way to the PC. Full e7 precision must survive.
  Wizard wizard = at_coverage_step();
  wizard.evaluate_coverage(nullptr, 0);
  char command[320]{};
  const size_t written =
      wizard.provision_command(command, sizeof(command), "oregon.manifest.json");
  CHECK(written > 0);
  CHECK(written < sizeof(command));
  CHECK(std::strstr(command, "provision_pack.py") != nullptr);
  CHECK(std::strstr(command, "--source-manifest oregon.manifest.json") != nullptr);
  CHECK(std::strstr(command, "--lat 44.0521000") != nullptr);
  CHECK(std::strstr(command, "--lon -123.0867000") != nullptr);
  CHECK(std::strstr(command, "--min-zoom 1") != nullptr);
  CHECK(std::strstr(command, "--max-zoom 13") != nullptr);
}

void test_provision_command_reports_truncation() {
  Wizard wizard = at_coverage_step();
  wizard.evaluate_coverage(nullptr, 0);
  char small[16]{};
  const size_t needed = wizard.provision_command(small, sizeof(small), "s.json");
  // snprintf semantics: the return is what WOULD have been written, so a
  // caller can tell the buffer was too small rather than silently shipping a
  // half command.
  CHECK(needed >= sizeof(small));
  CHECK(small[sizeof(small) - 1] == '\0');
}

void test_provision_command_without_a_source_is_still_actionable() {
  Wizard wizard = at_coverage_step();
  wizard.evaluate_coverage(nullptr, 0);
  char command[320]{};
  CHECK(wizard.provision_command(command, sizeof(command), nullptr) > 0);
  CHECK(std::strstr(command, "manifest.json") != nullptr);
  CHECK(wizard.provision_command(command, sizeof(command), "") > 0);
  CHECK(std::strstr(command, "manifest.json") != nullptr);
}

void test_begin_resets_previous_state() {
  Wizard wizard = at_coverage_step();
  const PackCoverage packs[] = {eugene_detail()};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);

  wizard.begin(false, 50);
  CHECK(wizard.state().step == Step::welcome);
  CHECK(!wizard.state().location_valid);
  CHECK(!wizard.state().covered);
  CHECK(!wizard.state().coverage_evaluated);
  CHECK(wizard.state().source == LocationSource::none);
  CHECK(wizard.state().latitude_e7 == 0);
  CHECK(wizard.state().radar_range_nm == 50);
  CHECK(!wizard.state().overview_available);
}

void test_messages_are_always_terminated() {
  Wizard wizard;
  wizard.begin(true, 25);
  const auto terminated = [](const char* text, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (text[i] == '\0') return true;
    }
    return false;
  };
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
  wizard.set_pin(0, 0);
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
  wizard.advance();
  wizard.set_pin(kEugeneLatE7, kEugeneLonE7);
  wizard.advance();
  wizard.advance();
  wizard.evaluate_coverage(nullptr, 0);
  CHECK(terminated(wizard.state().message, orcsdr::setup_wizard::kMessageSize));
  CHECK(terminated(wizard.state().recommendation.name,
                   orcsdr::setup_wizard::kPackNameSize));
}

void test_unterminated_pack_name_is_not_read_past() {
  // The name field is filled by the UI layer from a manifest on a removable
  // card, and may arrive with every byte used and no terminator.
  //
  // Scope of this test, stated honestly: it pins the OUTPUT contract --
  // terminated, and no longer than the field. It does NOT prove the bounded
  // read in copy_field is required. Reading past name[] lands in the
  // struct's own following members, so it stays inside the allocation and a
  // sanitizer cannot see it (verified: ASan passes either way), and because
  // the destination is the same size as the source field, truncation hides
  // any difference in output. copy_field is kept because the unbounded form
  // interprets adjacent coordinate bytes as text, not because this test
  // catches it.
  Wizard wizard = at_coverage_step();
  PackCoverage pack = eugene_detail();
  std::memset(pack.name, 'a', sizeof(pack.name));
  const PackCoverage packs[] = {pack};
  wizard.evaluate_coverage(packs, 1);
  CHECK(wizard.state().covered);
  // The copy must terminate within the destination and keep its full length.
  CHECK(wizard.state().covering_pack[orcsdr::setup_wizard::kPackNameSize - 1] ==
        '\0');
  CHECK(std::strlen(wizard.state().covering_pack) ==
        orcsdr::setup_wizard::kPackNameSize - 1);
}

}  // namespace

int main() {
  test_self_check();
  test_step_order_and_gating();
  test_back_navigation();
  test_unmoved_pin_is_rejected();
  test_out_of_range_coordinates_are_rejected();
  test_source_is_recorded();
  test_no_overview_routes_to_manual_entry();
  test_overview_pack_alone_is_not_coverage();
  test_detail_pack_containing_the_pin_is_coverage();
  test_detail_pack_elsewhere_is_not_coverage();
  test_pack_just_below_detail_zoom_is_not_coverage();
  test_pin_on_the_pack_boundary_counts_as_inside();
  test_changing_the_location_invalidates_the_verdict();
  test_coverage_without_a_location_does_not_claim_success();
  test_recommended_radius_covers_the_radar_range();
  test_radar_range_change_updates_the_recommendation();
  test_recommendation_asks_for_the_established_zoom_range();
  test_provision_command_carries_the_exact_coordinates();
  test_provision_command_reports_truncation();
  test_provision_command_without_a_source_is_still_actionable();
  test_begin_resets_previous_state();
  test_messages_are_always_terminated();
  test_unterminated_pack_name_is_not_read_past();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return EXIT_FAILURE;
  }
  std::puts("setup wizard tests passed");
  return EXIT_SUCCESS;
}
