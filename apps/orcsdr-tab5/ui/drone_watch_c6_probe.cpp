#include "drone_watch_c6_probe.hpp"
namespace orcsdr::drone_watch {
C6Capability assess_c6(const C6ProbeInputs& i) {
  C6Capability c{};
  c.link = i.link_failed ? C6Link::failed :
           i.link_ready ? C6Link::connected : C6Link::not_started;
  c.hosted_hci_compiled = i.hosted_hci_compiled;
  c.bluetooth_host_stack_compiled = i.bt_host_stack_compiled;
  c.cp_version_readable = i.cp_version_readable;
  // No C6 firmware's Bluetooth mode has been qualified for this OrcSDR build.
  c.cp_bt_controller_qualified = false;
  c.wifi_beacon_raw_capture_qualified = false;
  c.wifi_nan_capture_qualified = false;
  if (!i.hosted_hci_compiled) c.ble_gate = BleGate::host_hci_disabled;
  else if (!i.bt_host_stack_compiled) c.ble_gate = BleGate::host_stack_disabled;
  else if (c.link != C6Link::connected) c.ble_gate = BleGate::link_unavailable;
  else c.ble_gate = BleGate::firmware_not_qualified;
  return c;
}
bool same_c6_capability(const C6Capability& a, const C6Capability& b) {
  return a.link == b.link && a.hosted_hci_compiled == b.hosted_hci_compiled &&
         a.bluetooth_host_stack_compiled == b.bluetooth_host_stack_compiled &&
         a.cp_version_readable == b.cp_version_readable &&
         a.cp_bt_controller_qualified == b.cp_bt_controller_qualified &&
         a.ble_gate == b.ble_gate &&
         a.wifi_beacon_raw_capture_qualified == b.wifi_beacon_raw_capture_qualified &&
         a.wifi_nan_capture_qualified == b.wifi_nan_capture_qualified;
}
const char* c6_link_label(C6Link link) {
  switch (link) {
    case C6Link::connected: return "C6 LINK: HOSTED READY";
    case C6Link::failed: return "C6 LINK: FAILED";
    default: return "C6 LINK: NOT STARTED";
  }
}
const char* c6_ble_label(BleGate gate) {
  switch (gate) {
    case BleGate::host_hci_disabled: return "BLE: HCI BUILD DISABLED";
    case BleGate::host_stack_disabled: return "BLE: HOST STACK DISABLED";
    case BleGate::link_unavailable: return "BLE: LINK UNAVAILABLE";
    default: return "BLE: FIRMWARE UNVERIFIED";
  }
}
}
