// Host-only policy regression: g++ -std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui tests/lora_tx_policy_tests.cpp -o /tmp/orcsdr-lora-tx-policy && /tmp/orcsdr-lora-tx-policy
#include "lora_tx_policy.hpp"
#include <cassert>
#include <cstring>

using namespace orcsdr::lora_tx;

int main() {
  Capability c{};
  assert(can_arm(c) == Denial::radio_unverified);
  assert(can_send(c, 7) == Denial::radio_unverified);
  c.readiness = Readiness::profile_only;
  assert(can_arm(c) == Denial::radio_unverified);
  c.readiness = Readiness::detected;
  assert(can_arm(c) == Denial::radio_unverified);
  c.readiness = Readiness::rx_verified;
  assert(can_arm(c) == Denial::radio_unverified);
  c.readiness = Readiness::tx_verified;
  assert(can_arm(c) == Denial::antenna_unverified);
  c.antenna_path_verified = true;
  assert(can_arm(c) == Denial::region_unapproved);
  c.regional_tx_profile_approved = true;
  assert(can_arm(c) == Denial::frequency_unapproved);
  c.frequency_approved = true;
  c.frequency_hz = 915000000;
  assert(can_arm(c) == Denial::none);
  assert(can_send(c, 1) == Denial::not_armed);
  c.operator_armed = true;
  assert(can_send(c, 1) == Denial::invalid_payload);
  c.max_payload_bytes = 48;
  assert(can_send(c, 0) == Denial::invalid_payload);
  assert(can_send(c, 49) == Denial::invalid_payload);
  assert(can_send(c, 48) == Denial::none);
  c.busy = true;
  assert(can_arm(c) == Denial::busy);
  assert(can_send(c, 4) == Denial::busy);
  c.busy = false;
  c.operator_armed = false;
  assert(can_send(c, 4) == Denial::not_armed);
  c.readiness = Readiness::profile_only;
  c.operator_armed = true;
  assert(can_send(c, 4) == Denial::radio_unverified);
  assert(std::strcmp(readiness_name(Readiness::profile_only), "PROFILE ONLY") == 0);
  assert(std::strcmp(denial_name(Denial::radio_unverified), "TX NOT BENCH VERIFIED") == 0);
}
