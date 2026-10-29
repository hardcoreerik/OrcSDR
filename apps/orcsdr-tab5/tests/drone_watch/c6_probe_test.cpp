#include "drone_watch_c6_probe.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace orcsdr::drone_watch;
int main() {
  const C6Capability disabled = assess_c6({});
  assert(disabled.link == C6Link::not_started);
  assert(disabled.ble_gate == BleGate::host_hci_disabled);
  assert(!disabled.cp_bt_controller_qualified);
  assert(!disabled.wifi_beacon_raw_capture_qualified);
  assert(!disabled.wifi_nan_capture_qualified);
  assert(std::strcmp(c6_ble_label(disabled.ble_gate), "BLE: HCI BUILD DISABLED") == 0);
  assert(same_c6_capability(disabled, disabled));

  auto link = assess_c6({true, false, false, false, true});
  assert(link.link == C6Link::connected);
  assert(link.ble_gate == BleGate::host_hci_disabled); // link != HCI
  assert(!same_c6_capability(disabled, link));

  auto host_no_stack = assess_c6({true, false, true, false, true});
  assert(host_no_stack.ble_gate == BleGate::host_stack_disabled);
  auto cp_unknown = assess_c6({true, false, true, true, true});
  assert(cp_unknown.ble_gate == BleGate::firmware_not_qualified);
  assert(!cp_unknown.cp_bt_controller_qualified);
  assert(!cp_unknown.wifi_beacon_raw_capture_qualified);
  assert(!cp_unknown.wifi_nan_capture_qualified);
  auto disconnected = assess_c6({false, false, true, true, true});
  assert(disconnected.ble_gate == BleGate::link_unavailable);
  assert(disconnected.link == C6Link::not_started);
  auto failed = assess_c6({true, true, true, true, true});
  assert(failed.link == C6Link::failed);
  assert(failed.ble_gate == BleGate::link_unavailable); // failure dominates stale link
  assert(!same_c6_capability(failed, cp_unknown));
  std::cout << "Drone Watch P2a C6 passive capability diagnosis PASS; no acquisition configured\n";
}
