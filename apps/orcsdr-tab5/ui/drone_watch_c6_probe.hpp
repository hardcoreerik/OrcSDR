#pragma once
// Passive, read-only ESP-Hosted C6 capability diagnosis for Drone Watch.
// Does NOT initialize ESP-Hosted, start Bluetooth, scan Wi-Fi, flash firmware
// or claim actual reception. All fields are observations or compile-time flags.
#include <cstdint>
namespace orcsdr::drone_watch {
enum class C6Link : uint8_t { not_started, connected, failed };
enum class BleGate : uint8_t {
  host_hci_disabled, host_stack_disabled, link_unavailable,
  firmware_not_qualified
};
struct C6Capability {
  C6Link link = C6Link::not_started;
  bool hosted_hci_compiled = false;
  bool bluetooth_host_stack_compiled = false;
  bool cp_version_readable = false;
  bool cp_bt_controller_qualified = false;  // NEVER inferred from Wi-Fi version
  BleGate ble_gate = BleGate::host_hci_disabled;
  // Wi-Fi Beacon/NAN is not automatically available from station AP scan.
  bool wifi_beacon_raw_capture_qualified = false;
  bool wifi_nan_capture_qualified = false;
};
struct C6ProbeInputs {
  bool link_ready = false;
  bool link_failed = false;
  bool hosted_hci_compiled = false;
  bool bt_host_stack_compiled = false;
  bool cp_version_readable = false;
};
// Evaluating build config/link state does NOT prove co-processor BLE support.
// There is intentionally NO "supported/receiving" state before hardware evidence.
C6Capability assess_c6(const C6ProbeInputs& inputs);
bool same_c6_capability(const C6Capability& a, const C6Capability& b);
const char* c6_link_label(C6Link link);
const char* c6_ble_label(BleGate gate);
}
