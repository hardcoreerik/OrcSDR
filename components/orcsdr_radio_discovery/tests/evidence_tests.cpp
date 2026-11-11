#include "orcsdr_radio_discovery/evidence.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>

using namespace orcsdr::radio_discovery;

static Observation make_rf() {
  Observation o{};
  o.protocol = Protocol::dmr_tier2;
  o.origin = Origin::rf_observation;
  o.proof = Proof::crc_valid_decode;
  o.frequency_hz = 446500000;
  o.identity.access_code = 1;
  o.identity.slot = 2;
  o.identity.destination_id = 99;
  o.identity.known_fields = access_code_known | slot_known | destination_known;
  return o;
}

static void test_rf_does_not_equal_directory() {
  auto o = make_rf();
  assert(valid(o));
  assert(is_rf_proven(o));
  o.origin = Origin::directory_import;
  assert(!valid(o));
  assert(!is_rf_proven(o));
  o.proof = Proof::candidate;
  assert(valid(o));
  o.origin = Origin::user_entry;
  assert(valid(o));
}

static void test_identity_and_proof_gates() {
  auto o = make_rf();
  o.identity.slot = 3;
  assert(!valid(o));
  o.identity.slot = 1;
  assert(valid(o));
  o.identity.access_code = 16;
  assert(!valid(o));
  o.identity.access_code = 0; // Zero is a valid DMR color code when flagged.
  assert(valid(o));
  o.proof = Proof::site_confirmed;
  assert(!valid(o));
  o.identity.known_fields |= site_known;
  o.identity.site_id = 3;
  assert(valid(o));
  o.protocol = Protocol::unknown;
  assert(!valid(o));
}

static void test_location_and_time() {
  auto o = make_rf();
  o.receiver_location.known = true;
  o.receiver_location.latitude_e7 = 450000000;
  o.receiver_location.longitude_e7 = -1230000000;
  assert(valid(o));
  o.receiver_location.latitude_e7 = 900000001;
  assert(!valid(o));
  o.receiver_location.known = false;
  o.utc_known = true;
  assert(!valid(o));
  o.utc_ms = 1794440000000ULL;
  assert(valid(o));
}

static void test_bounded_order_and_rejection() {
  ObservationRing<2> r;
  auto o = make_rf();
  assert(r.append(o));
  o.frequency_hz = 446600000;
  assert(r.append(o));
  o.frequency_hz = 446700000;
  assert(r.append(o));
  assert(r.size() == 2);
  assert(r.newest(0)->frequency_hz == 446700000);
  assert(r.newest(1)->frequency_hz == 446600000);
  assert(r.newest(2) == nullptr);
  o.frequency_hz = 0;
  assert(!r.append(o));
  assert(r.size() == 2);
  r.clear();
  assert(r.size() == 0 && r.newest() == nullptr);
}

int main() {
  static_assert(sizeof(ObservationRing<8>) < 4096, "F0 test history exceeded 4KB");
  test_rf_does_not_equal_directory();
  test_identity_and_proof_gates();
  test_location_and_time();
  test_bounded_order_and_rejection();
  std::puts("radio_discovery_evidence_tests: PASS");
}
