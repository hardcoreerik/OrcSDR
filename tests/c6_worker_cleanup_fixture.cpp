#include "c6_recovery_app.cpp"

namespace {
TaskFunction_t captured_task = nullptr;
int64_t fake_time_us = 0;
int64_t release_start_us = 0;
int64_t first_failed_elapsed_us = -1;
int reset_calls = 0;
int hosted_release_calls = 0;
int flash_calls = 0;
int releasing_ticks = 0;
int delay_calls = 0;
bool failed_phase_seen = false;
bool install_refused = false;
bool retained_until_idle = true;
bool timed_out_release_observed = false;
bool probe_ack_received = false;
bool recovery_idle = false;
bool ack_race_mode = false;
bool disconnect_ack_accepted = false;
bool disconnect_before_reset = false;
bool delay_abort = false;
struct DelayAbort {};
enum class Scenario { prepare_failure, ack_race, install_success, stale_generation, reset_failure };
Scenario scenario = Scenario::prepare_failure;
wifi_app::Status mock_wifi_status{};
int64_t stale_wait_start_us = -1;
int stale_wait_delays = 0;
bool boot_ack_sent = false;
bool reset_should_fail = false;
wifi_app::RecoveryAccess access_state = wifi_app::RecoveryAccess::ready;
bool package_available = true;
const char* profile_name = "lilygo-t-display-p4-amoled";
void reset_observations() {
  orcsdr::c6_update::release(orcsdr::c6_update::Owner::rom);
  c6_recovery_app::busy = false;
  c6_recovery_app::phase = c6_recovery_app::Phase::idle;
  c6_recovery_app::decision = 0;
  captured_task = nullptr;
  fake_time_us = 0;
  release_start_us = 0;
  first_failed_elapsed_us = -1;
  reset_calls = hosted_release_calls = flash_calls = releasing_ticks = 0;
  delay_calls = 0;
  scenario = Scenario::prepare_failure;
  mock_wifi_status = {};
  mock_wifi_status.generation = 27;
  stale_wait_start_us = -1;
  stale_wait_delays = 0;
  boot_ack_sent = false;
  reset_should_fail = false;
  failed_phase_seen = install_refused = probe_ack_received = false;
  retained_until_idle = true;
  timed_out_release_observed = false;
  recovery_idle = false;
  ack_race_mode = disconnect_ack_accepted = disconnect_before_reset = delay_abort = false;
  access_state = wifi_app::RecoveryAccess::ready;
}
}

extern "C" const char* orcsdr_embedded_c6_rom_version() { return "3.9.7"; }
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
C6UpdateStatus c6_update_status() { return {C6UpdateState::current}; }
}
namespace wifi_app {
bool request_recovery_access() { return true; }
RecoveryAccess recovery_access() { return access_state; }
void release_recovery_access() {
  ++hosted_release_calls;
  release_start_us = fake_time_us;
  access_state = RecoveryAccess::releasing;
}
Status status() { return mock_wifi_status; }
}
extern "C" void orc_c6_rom_loader_session_init(orc_c6_rom_loader_session_t*,
    esp_loader*, void*, void*, const orc_c6_rom_board_t*) {}
extern "C" void orc_c6_rom_loader_disconnect(orc_c6_rom_loader_session_t*) {
  if (!ack_race_mode) return;
  disconnect_before_reset = reset_calls == 0 && hosted_release_calls == 0 &&
      c6_recovery_app::status().busy &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::rom;
  disconnect_ack_accepted = c6_recovery_app::confirm_boot_released(false) &&
      c6_recovery_app::decision.load() == 2;
}
extern "C" int orc_c6_rom_loader_connect_callback(void*, uint32_t*, uint32_t*, bool*) { return -1; }
extern "C" int orc_c6_rom_loader_write_callback(void*, uint32_t, const uint8_t*, size_t) { return -1; }
extern "C" int orc_c6_rom_loader_sha256_callback(void*, const uint8_t*, size_t, uint8_t*) { return -1; }
extern "C" int orc_c6_rom_loader_sha256_flash_callback(void*, uint32_t, size_t, uint8_t*) { return -1; }
extern "C" orc_c6_rom_result_t orc_c6_rom_prepare(orc_c6_rom_update_t*,
    const orc_c6_rom_manifest_t*, const orc_c6_rom_ops_t* ops) {
  if (scenario == Scenario::prepare_failure || scenario == Scenario::reset_failure)
    return ORC_C6_ROM_CONNECT_FAILED;
  ops->progress(ops->context, ORC_C6_ROM_PHASE_RELEASE_BOOT, 100);
  return ORC_C6_ROM_OK;
}
extern "C" orc_c6_rom_result_t orc_c6_rom_flash(orc_c6_rom_update_t*) {
  ++flash_calls;
  return scenario == Scenario::install_success || scenario == Scenario::stale_generation
      ? ORC_C6_ROM_OK : ORC_C6_ROM_BAD_STATE;
}
BaseType_t xTaskCreate(TaskFunction_t function, const char*, uint32_t, void*, UBaseType_t,
                       TaskHandle_t*) {
  captured_task = function;
  return pdPASS;
}
int64_t esp_timer_get_time() { return fake_time_us; }
esp_err_t orcsdr_board_c6_enable(bool) {
  ++reset_calls;
  return reset_should_fail ? -1 : ESP_OK;
}
void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks) {
  if (++delay_calls > 7000) { delay_abort = true; throw DelayAbort{}; }
  if (access_state == wifi_app::RecoveryAccess::releasing &&
      c6_recovery_app::status().phase == c6_recovery_app::Phase::failed &&
      first_failed_elapsed_us < 0) {
    first_failed_elapsed_us = fake_time_us - release_start_us;
    if (first_failed_elapsed_us <= 40000000) retained_until_idle = false;
  }
  const bool delayed_release = access_state == wifi_app::RecoveryAccess::releasing &&
      (scenario == Scenario::prepare_failure || scenario == Scenario::ack_race);
  if (scenario == Scenario::stale_generation && access_state == wifi_app::RecoveryAccess::idle &&
      flash_calls == 1) {
    if (stale_wait_start_us < 0) stale_wait_start_us = fake_time_us;
    ++stale_wait_delays;
    fake_time_us += 15000000;
  } else {
    fake_time_us += delayed_release ? 15000000 : static_cast<int64_t>(ticks) * 1000;
  }
  using c6_recovery_app::Phase;
  if (!boot_ack_sent && scenario != Scenario::ack_race &&
      (scenario == Scenario::install_success || scenario == Scenario::stale_generation) &&
      c6_recovery_app::status().phase == Phase::release_boot) {
    boot_ack_sent = c6_recovery_app::confirm_boot_released(true) &&
        c6_recovery_app::decision.load() == 1;
  }
  if (c6_recovery_app::status().phase == Phase::release_boot_failure && !failed_phase_seen) {
    failed_phase_seen = true;
    install_refused = !c6_recovery_app::confirm_boot_released(true) &&
        c6_recovery_app::decision.load() == 0;
    if (reset_calls != 0 || hosted_release_calls != 0 || !c6_recovery_app::status().busy ||
        orcsdr::c6_update::owner.load() != orcsdr::c6_update::Owner::rom) {
      retained_until_idle = false;
    }
    if (!c6_recovery_app::confirm_boot_released(false) ||
        c6_recovery_app::decision.load() != 2) retained_until_idle = false;
    else probe_ack_received = true;
  }
  if (access_state == wifi_app::RecoveryAccess::releasing && !recovery_idle) {
    ++releasing_ticks;
    if (!c6_recovery_app::status().busy ||
        orcsdr::c6_update::owner.load() != orcsdr::c6_update::Owner::rom) {
      retained_until_idle = false;
    }
    if (c6_recovery_app::status().phase == Phase::failed &&
        fake_time_us - release_start_us > 40000000) {
      timed_out_release_observed = c6_recovery_app::status().busy &&
          orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::rom;
    }
    if (releasing_ticks == 4) {
      access_state = wifi_app::RecoveryAccess::idle;
      recovery_idle = true;
      if (scenario == Scenario::install_success || scenario == Scenario::stale_generation) {
        mock_wifi_status.started = true;
        std::strcpy(mock_wifi_status.c6_version, "3.9.7");
        if (scenario == Scenario::install_success) ++mock_wifi_status.generation;
      }
    }
  }
}

extern "C" __declspec(dllexport) int run_prepare_failure_cleanup() {
  reset_observations();
  if (!c6_recovery_app::begin() || !captured_task) return 1;
  captured_task(nullptr);
  const auto final = c6_recovery_app::status();
  return failed_phase_seen && install_refused && probe_ack_received && first_failed_elapsed_us > 40000000 &&
      retained_until_idle && timed_out_release_observed && recovery_idle &&
      releasing_ticks == 4 && reset_calls == 2 && hosted_release_calls == 1 && flash_calls == 0 &&
      final.phase == c6_recovery_app::Phase::failed && !final.busy &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::none ? 0 : 2;
}
extern "C" __declspec(dllexport) int run_disconnect_ack_race() {
  reset_observations();
  scenario = Scenario::ack_race;
  ack_race_mode = true;
  if (!c6_recovery_app::begin() || !captured_task) return 1;
  try {
    captured_task(nullptr);
  } catch (const DelayAbort&) {
    return 2;
  }
  const auto final = c6_recovery_app::status();
  return !delay_abort && delay_calls <= 7000 && disconnect_before_reset && disconnect_ack_accepted &&
      hosted_release_calls == 1 && reset_calls == 2 && flash_calls == 0 && recovery_idle &&
      final.phase == c6_recovery_app::Phase::failed && !final.busy &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::none ? 0 : 3;
}
extern "C" __declspec(dllexport) int run_install_success() {
  reset_observations();
  scenario = Scenario::install_success;
  if (!c6_recovery_app::begin() || !captured_task) return 1;
  try { captured_task(nullptr); } catch (const DelayAbort&) { return 2; }
  const auto final = c6_recovery_app::status();
  return !delay_abort && boot_ack_sent && flash_calls == 1 && mock_wifi_status.started &&
      mock_wifi_status.generation == 28 && std::strcmp(mock_wifi_status.c6_version, "3.9.7") == 0 &&
      final.phase == c6_recovery_app::Phase::complete && final.result == 0 && !final.busy &&
      hosted_release_calls == 1 && reset_calls == 2 && recovery_idle &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::none ? 0 : 3;
}
extern "C" __declspec(dllexport) int run_stale_generation_rejected() {
  reset_observations();
  scenario = Scenario::stale_generation;
  if (!c6_recovery_app::begin() || !captured_task) return 1;
  try { captured_task(nullptr); } catch (const DelayAbort&) { return 2; }
  const auto final = c6_recovery_app::status();
  return !delay_abort && boot_ack_sent && flash_calls == 1 && mock_wifi_status.started &&
      mock_wifi_status.generation == 27 && std::strcmp(mock_wifi_status.c6_version, "3.9.7") == 0 &&
      stale_wait_start_us >= 0 && stale_wait_delays >= 4 &&
      fake_time_us - stale_wait_start_us > 45000000 && final.phase == c6_recovery_app::Phase::failed &&
      final.result == -6 && !final.busy && hosted_release_calls == 1 && reset_calls == 2 && recovery_idle &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::none ? 0 : 3;
}
extern "C" __declspec(dllexport) int run_reset_failure_cleanup() {
  reset_observations();
  scenario = Scenario::reset_failure;
  reset_should_fail = true;
  if (!c6_recovery_app::begin() || !captured_task) return 1;
  try { captured_task(nullptr); } catch (const DelayAbort&) { return 2; }
  const auto final = c6_recovery_app::status();
  return failed_phase_seen && probe_ack_received && flash_calls == 0 && reset_calls == 2 &&
      hosted_release_calls == 1 && recovery_idle && final.phase == c6_recovery_app::Phase::failed &&
      final.result == -4 && !final.busy &&
      orcsdr::c6_update::owner.load() == orcsdr::c6_update::Owner::none ? 0 : 3;
}
