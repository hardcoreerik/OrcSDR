#include "c6_recovery_app.cpp"

namespace {
bool package_available = true;
const char* profile_name = "lilygo-t-display-p4-amoled";
bool task_fails = false;
orcsdr::wifi::C6UpdateState ota_state = orcsdr::wifi::C6UpdateState::current;
void reset_guard_state() {
  orcsdr::c6_update::release(orcsdr::c6_update::Owner::rom);
  orcsdr::c6_update::release(orcsdr::c6_update::Owner::hosted);
  c6_recovery_app::busy = false;
  c6_recovery_app::phase = c6_recovery_app::Phase::idle;
  c6_recovery_app::decision = 0;
}
bool begin_in_phase(c6_recovery_app::Phase expected_phase) {
  reset_guard_state();
  if (!c6_recovery_app::begin()) return false;
  if (c6_recovery_app::status().phase != c6_recovery_app::Phase::quiescing ||
      c6_recovery_app::decision.load() != 0) return false;
  c6_recovery_app::phase = expected_phase;
  return true;
}
}

extern "C" const char* orcsdr_embedded_c6_rom_version() { return "3.0.6"; }
extern "C" const orc_c6_rom_manifest_t* orcsdr_embedded_c6_rom_manifest() {
  static const orc_c6_rom_manifest_t manifest{};
  return package_available ? &manifest : nullptr;
}
const orcsdr_board_profile_t* orcsdr_board_profile() {
  static orcsdr_board_profile_t profile{};
  profile.name = profile_name;
  profile.c6_rom_sdio_recovery = std::strcmp(profile_name,"lilygo-t-display-p4-amoled")==0;
  return &profile;
}
namespace orcsdr::wifi {
C6UpdateStatus c6_update_status() { return {ota_state}; }
}
namespace wifi_app {
bool request_recovery_access() { return false; }
RecoveryAccess recovery_access() { return RecoveryAccess::idle; }
void release_recovery_access() {}
Status status() { return {}; }
}
extern "C" void orc_c6_rom_loader_session_init(orc_c6_rom_loader_session_t*,
    esp_loader*, void*, void*, const orc_c6_rom_board_t*) {}
extern "C" void orc_c6_rom_loader_disconnect(orc_c6_rom_loader_session_t*) {}
extern "C" int orc_c6_rom_loader_connect_callback(void*, uint32_t*, uint32_t*, bool*) { return -1; }
extern "C" int orc_c6_rom_loader_write_callback(void*, uint32_t, const uint8_t*, size_t) { return -1; }
extern "C" int orc_c6_rom_loader_sha256_callback(void*, const uint8_t*, size_t, uint8_t*) { return -1; }
extern "C" int orc_c6_rom_loader_sha256_flash_callback(void*, uint32_t, size_t, uint8_t*) { return -1; }
extern "C" orc_c6_rom_result_t orc_c6_rom_prepare(orc_c6_rom_update_t*,
    const orc_c6_rom_manifest_t*, const orc_c6_rom_ops_t*) { return ORC_C6_ROM_CONNECT_FAILED; }
extern "C" orc_c6_rom_result_t orc_c6_rom_flash(orc_c6_rom_update_t*) {
  return ORC_C6_ROM_BAD_STATE;
}
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t,
                       TaskHandle_t*) { return task_fails ? 0 : 1; }

extern "C" __declspec(dllexport) int check_prerequisites() {
  using namespace c6_recovery_app;
  reset_guard_state();
  package_available = false;
  if (begin()) return 1;
  package_available = true;
  profile_name = "other-profile";
  if (begin()) return 2;
  profile_name = "lilygo-t-display-p4-amoled";
  ota_state = orcsdr::wifi::C6UpdateState::updating;
  if (begin()) return 3;
  ota_state = orcsdr::wifi::C6UpdateState::rebooting;
  if (begin()) return 4;
  ota_state = orcsdr::wifi::C6UpdateState::current;
  if (!orcsdr::c6_update::claim(orcsdr::c6_update::Owner::hosted)) return 5;
  if (begin() || orcsdr::c6_update::owner.load() != orcsdr::c6_update::Owner::hosted) return 6;
  orcsdr::c6_update::release(orcsdr::c6_update::Owner::rom);
  if (orcsdr::c6_update::owner.load() != orcsdr::c6_update::Owner::hosted) return 7;
  orcsdr::c6_update::release(orcsdr::c6_update::Owner::hosted);
  if (!begin() || status().phase != Phase::quiescing || decision.load() != 0 ||
      orcsdr::c6_update::owner.load() != orcsdr::c6_update::Owner::rom) return 8;
  busy = false;
  return 0;
}
extern "C" __declspec(dllexport) int check_task_failure() {
  using namespace c6_recovery_app;
  reset_guard_state();
  task_fails = true;
  const bool accepted = begin();
  task_fails = false;
  return !accepted && !status().busy && status().phase == Phase::failed &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::none ? 0 : 1;
}
extern "C" __declspec(dllexport) int check_release_guard() {
  using namespace c6_recovery_app;
  reset_guard_state();
  if (confirm_boot_released(true) || confirm_boot_released(false)) return 1;
  if (!begin_in_phase(Phase::idle)) return 2;
  // A release phase without an active operation must not accept confirmation.
  busy = false;
  phase = Phase::release_boot;
  if (confirm_boot_released(true) || confirm_boot_released(false)) return 3;
  if (!begin_in_phase(Phase::release_boot)) return 4;
  if (!confirm_boot_released(false) || decision.load() != 2 || confirm_boot_released(true)) return 5;
  if (!begin_in_phase(Phase::release_boot)) return 6;
  if (!confirm_boot_released(true) || decision.load() != 1 || confirm_boot_released(false)) return 7;
  if (!begin_in_phase(Phase::release_boot_failure)) return 8;
  if (confirm_boot_released(true) || decision.load() != 0) return 9;
  if (!confirm_boot_released(false) || decision.load() != 2 || confirm_boot_released(false)) return 10;
  if (!begin_in_phase(Phase::release_boot_failure)) return 11;
  decision=3; // cancellation observed before or during failure publication
  if(confirm_boot_released(true) || !confirm_boot_released(false) || decision.load()!=2) return 12;
  busy = false;
  return 0;
}
extern "C" __declspec(dllexport) int check_duplicate_cancel() {
  using namespace c6_recovery_app;
  if (begin_in_phase(Phase::quiescing) == false || begin() || !cancel() || decision.load() != 3 || cancel()) return 1;
  const Phase allowed[] = {Phase::quiescing, Phase::connecting, Phase::release_boot};
  for (const auto p : allowed) {
    if (!begin_in_phase(p) || !cancel() || decision.load() != 3 || cancel()) return 2;
  }
  const Phase refused[] = {Phase::idle, Phase::verifying, Phase::writing, Phase::complete};
  for (const auto p : refused) {
    reset_guard_state();
    busy = true;
    phase = p;
    if (cancel() || decision.load() != 0) return 3;
  }
  // A non-idle release phase also refuses cancellation when no operation owns it.
  reset_guard_state();
  phase = Phase::release_boot;
  if (cancel()) return 4;
  return 0;
}
extern "C" __declspec(dllexport) int check_cancel_after_write() {
  using namespace c6_recovery_app;
  if (!begin_in_phase(Phase::writing)) return 1;
  phase = Phase::writing;
  if (cancel() || decision.load() != 0) return 2;
  busy = false;
  return 0;
}
