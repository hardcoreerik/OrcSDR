"""Host regression for actual reconnect scheduling/timer code; no hardware."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <atomic>
#include <cstdint>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static bool settings_wifi_power_enabled = true, settings_wifi_start_at_boot;
static bool wifi_auto_reconnect_armed, wifi_connecting, wifi_connected, wifi_scan_running;
static bool wifi_save_after_connect;
static unsigned wifi_profile_count = 1, wifi_reconnect_attempts;
static uint32_t wifi_reconnect_due_ms, clock_ms = 1000;
static constexpr uint32_t kWifiReconnectBackoffMs[] = {1000, 3000};
static char wifi_ssid[] = "x";
static std::atomic<bool> wifi_connect_requested;
static std::atomic<bool> wifi_scan_requested;
static bool wifi_station_ready = true, wifi_connect_radio_paused;
static uint32_t wifi_link_recovery_due_ms, wifi_connected_since_ms, wifi_scan_revision;
static unsigned wifi_link_recoveries;
static constexpr unsigned kWifiLinkRecoveryMax = 2;
static char wifi_status_message[100];
static int scan_calls, connect_calls, recovery_calls;
namespace orcsdr::wifi {
static bool failed;
static bool link_failed() { return failed; }
}
static uint32_t millis() { return clock_ms; }
static void select_wifi_profile(unsigned) {}
static void resume_radio_after_io(bool& paused) { paused = false; }
static void draw_wifi_state() {}
static void start_wifi_inventory() { ++scan_calls; }
static void start_wifi_connection() { ++connect_calls; }
static void recover_wifi_link() {
  ++recovery_calls; ++wifi_link_recoveries;
  orcsdr::wifi::failed = false; wifi_station_ready = true;
}
static size_t strlcpy(char* out, const char* text, size_t size) {
  std::snprintf(out, size, "%s", text); return std::strlen(text);
}
static struct { void printf(const char*, ...) {} void println(const char*) {} } Serial;
/* SCHEDULE */
/* POLL PREFIX */
static void timer() {
  /* TIMER */
}
static void require(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
  wifi_auto_reconnect_armed = true;
  schedule_wifi_reconnect("manual_connection_failed");
  require(wifi_reconnect_due_ms == 2000, "Manual connection with boot off did not schedule retry");
  clock_ms = 2000; timer();
  require(wifi_connect_requested.load(), "Scheduled manual retry with boot off was discarded");
  wifi_reconnect_due_ms = 0; wifi_reconnect_attempts = 0;
  wifi_auto_reconnect_armed = false; settings_wifi_start_at_boot = true;
  schedule_wifi_reconnect("after_manual_disconnect");
  require(!wifi_reconnect_due_ms, "Boot preference overrode manual disconnect");
  wifi_auto_reconnect_armed = true; settings_wifi_power_enabled = false;
  schedule_wifi_reconnect("power_off");
  require(!wifi_reconnect_due_ms, "Power-off session retried");
  settings_wifi_power_enabled = true; wifi_profile_count = 0;
  schedule_wifi_reconnect("no_profile");
  require(!wifi_reconnect_due_ms, "Missing-profile session retried");
  wifi_profile_count = 1; wifi_reconnect_attempts = 2;
  schedule_wifi_reconnect("retry_budget_exhausted");
  require(!wifi_reconnect_due_ms, "Retry budget was ignored");
  std::puts("Five reconnect policy checks passed");
  wifi_reconnect_attempts = 0; wifi_auto_reconnect_armed = false;
  wifi_connected = false; wifi_connecting = false; wifi_scan_running = false;
  settings_wifi_start_at_boot = false; clock_ms = 5000;
  wifi_connect_requested.store(true); wifi_scan_requested.store(true);
  orcsdr::wifi::failed = true; poll_prefix();
  require(wifi_link_recovery_due_ms == 7000, "Offline transport failure was not scheduled for recovery");
  require(!scan_calls && !connect_calls, "Dead transport received a normal Wi-Fi request");
  clock_ms = 6000; poll_prefix();
  require(wifi_link_recovery_due_ms == 7000, "Repeated failure polling postponed recovery");
  clock_ms = 7000; poll_prefix();
  require(recovery_calls == 1 && connect_calls == 1 && scan_calls == 1,
          "Queued requests did not resume after recovery");
  wifi_connected = true; wifi_link_recoveries = kWifiLinkRecoveryMax;
  orcsdr::wifi::failed = true; poll_prefix();
  require(!wifi_link_recovery_due_ms && !wifi_connected, "Recovery budget exhaustion was ignored");
  poll_prefix();
  require(recovery_calls == 1, "Exhausted recovery repeated");
  std::puts("Five offline recovery checks passed");
}
'''


def main():
    text = (ROOT / 'apps/orcsdr-tab5/ui/main.cpp').read_text()
    start = text.index('void schedule_wifi_reconnect(const char* why) {')
    end = text.index('\nvoid recover_wifi_link()', start)
    schedule = text[start:end]
    start = text.index('  if (wifi_reconnect_due_ms != 0 &&')
    end = text.index('  if (wifi_scan_requested.exchange', start)
    timer = text[start:end]
    start = text.index('void poll_wifi() {')
    end = text.index('  static uint32_t last_wifi_status_ms', start)
    prefix = text[start:end].replace('void poll_wifi()', 'void poll_prefix()', 1) + '\n}\n'
    build = ROOT / 'apps/orcsdr-tab5/build-wifi-reconnect-host'
    source = build / 'source'
    source.mkdir(parents=True, exist_ok=True)
    (source / 'policy.cpp').write_text(HARNESS.replace('/* SCHEDULE */', schedule).replace('/* TIMER */', timer).replace('/* POLL PREFIX */', prefix))
    (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(wifi_reconnect_host CXX)\nset(CMAKE_CXX_STANDARD 17)\nadd_executable(policy policy.cpp)\nenable_testing()\nadd_test(NAME wifi_reconnect_policy COMMAND policy)\n')
    for command in (["cmake", "-S", str(source), "-B", str(build)],
                    ["cmake", "--build", str(build), "--config", "Debug"],
                    ["ctest", "--test-dir", str(build), "-C", "Debug", "--output-on-failure"]):
        result = subprocess.run(command, timeout=120)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
