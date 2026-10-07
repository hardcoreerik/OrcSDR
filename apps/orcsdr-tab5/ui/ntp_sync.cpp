#include "ntp_sync.hpp"

#include "time_service.hpp"

#include <esp_sntp.h>
#include <esp_timer.h>

#include <ctime>

namespace orcsdr::ntp_sync {
namespace {

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

constexpr uint32_t kTimeoutMs = 20000;
constexpr const char* kServer = "pool.ntp.org";

State g_state = State::idle;
uint32_t g_started_ms = 0;
bool g_result_pending = false;
bool g_result_ok = false;

void finish(bool ok) {
  esp_sntp_stop();
  g_state = ok ? State::done : State::failed;
  g_result_ok = ok;
  g_result_pending = true;
}

}  // namespace

bool start() {
  if (g_state == State::syncing) return false;
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, kServer);
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  esp_sntp_init();
  g_started_ms = now_ms();
  g_state = State::syncing;
  return true;
}

void poll() {
  if (g_state != State::syncing) return;
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    const time_t now = time(nullptr);
    finish(now > 0 && time_service::set_utc(static_cast<uint32_t>(now)));
  } else if (now_ms() - g_started_ms > kTimeoutMs) {
    finish(false);
  }
}

State state() { return g_state; }

bool take_result(bool* ok) {
  if (!g_result_pending) return false;
  g_result_pending = false;
  if (ok != nullptr) *ok = g_result_ok;
  return true;
}

}  // namespace orcsdr::ntp_sync
