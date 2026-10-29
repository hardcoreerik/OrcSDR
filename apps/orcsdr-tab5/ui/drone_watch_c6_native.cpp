#include "drone_watch_c6_native.hpp"
#include "wifi_service.hpp"
#include "sdkconfig.h"
#include <cstring>
namespace orcsdr::drone_watch {
C6Capability inspect_c6_without_side_effects() {
  C6ProbeInputs inputs{};
  inputs.link_ready = wifi::hosted_transport_ready();
  inputs.link_failed = wifi::link_failed();
#if defined(CONFIG_ESP_HOSTED_HOST_FEAT_BT) && CONFIG_ESP_HOSTED_HOST_FEAT_BT
  inputs.hosted_hci_compiled = true;
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
  inputs.bt_host_stack_compiled = true;
#elif defined(CONFIG_BT_BLUEDROID_ENABLED) && CONFIG_BT_BLUEDROID_ENABLED
  inputs.bt_host_stack_compiled = true;
#endif
  // Wi-Fi's readable Hosted firmware string gives diagnostic context only;
  // it is NOT proof of BLE controller firmware or HCI mode.
  const char* version = wifi::hosted_c6_version();
  inputs.cp_version_readable = version && version[0] &&
        std::strcmp(version, "unavailable") != 0;
  return assess_c6(inputs);
}
}
