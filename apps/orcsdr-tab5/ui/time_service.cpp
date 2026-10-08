#include "time_service.hpp"

#include <M5Unified.h>

#include "clock_settings.hpp"
#include "nvs_store.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace orcsdr::time_service {
namespace {

constexpr time_t kMinUtc = 1704067200;  // 2024-01-01
constexpr time_t kMaxUtc = 4102444800;  // 2100-01-01
std::atomic<bool> g_wallclock_valid{false};
std::atomic<int32_t> g_offset_minutes{0};
constexpr const char* kOffsetKey = "tz_off_min";

bool valid_epoch(time_t value) { return value >= kMinUtc && value < kMaxUtc; }

}  // namespace

void initialize(bool previously_established) {
  m5::rtc_datetime_t value{};
  const bool calendar_valid = M5.Rtc.isEnabled() && M5.Rtc.getDateTime(&value) &&
                              value.date.year >= 2024 && value.date.year <= 2099;
  if (previously_established && calendar_valid) {
    // The RTC only has one-second resolution, so reading it at a random moment leaves the system clock behind by up to a
    // second. FT8 needs well under half a second: wait for the RTC's seconds to tick (at most about 1.1 s at boot) and
    // take the time right then, which puts the system clock within a few milliseconds of the RTC's own tick.
    m5::rtc_datetime_t tick{};
    const uint8_t first_second = value.time.seconds;
    const uint32_t started = millis();
    while (millis() - started < 1100u) {
      if (M5.Rtc.getDateTime(&tick) && tick.time.seconds != first_second) break;
      delay(1);
    }
    M5.Rtc.setSystemTimeFromRtc();
  }
  g_wallclock_valid.store(previously_established && calendar_valid &&
                              valid_epoch(time(nullptr)),
                          std::memory_order_release);
}

Snapshot now() {
  Snapshot result{millis(), 0, g_wallclock_valid.load(std::memory_order_acquire)};
  if (!result.wallclock_valid) return result;
  const time_t value = time(nullptr);
  if (!valid_epoch(value)) {
    g_wallclock_valid.store(false, std::memory_order_release);
    result.wallclock_valid = false;
    return result;
  }
  result.utc = static_cast<uint32_t>(value);
  return result;
}

bool set_utc(uint32_t epoch) {
  const time_t requested = static_cast<time_t>(epoch);
  if (!M5.Rtc.isEnabled() || !valid_epoch(requested)) return false;
  tm utc{};
  if (gmtime_r(&requested, &utc) == nullptr) return false;
  M5.Rtc.setDateTime(&utc);
  m5::rtc_datetime_t stored{};
  if (!M5.Rtc.getDateTime(&stored) || stored.date.year != utc.tm_year + 1900 ||
      stored.date.month != utc.tm_mon + 1 || stored.date.date != utc.tm_mday ||
      stored.time.hours != utc.tm_hour || stored.time.minutes != utc.tm_min ||
      stored.time.seconds != utc.tm_sec) {
    g_wallclock_valid.store(false, std::memory_order_release);
    return false;
  }
  M5.Rtc.setSystemTimeFromRtc();
  const time_t readback = time(nullptr);
  const bool valid = valid_epoch(readback) &&
                     std::llabs(static_cast<long long>(readback - requested)) <= 2;
  g_wallclock_valid.store(valid, std::memory_order_release);
  return valid;
}

int32_t utc_offset_minutes() { return g_offset_minutes.load(std::memory_order_acquire); }

bool set_utc_offset_minutes(int32_t minutes) {
  if (clock_settings::clamp_offset(minutes) != minutes) return false;   // reject, never silently clamp
  g_offset_minutes.store(minutes, std::memory_order_release);
  return true;
}

void load_config(NvsStore& store) {
  const int32_t stored = store.get_i32(kOffsetKey, 0);
  // An out-of-range stored value is ignored (UTC), not shown as a legal zone.
  g_offset_minutes.store(clock_settings::valid_offset(stored) ? stored : 0, std::memory_order_release);
}

bool save_config(NvsStore& store) {
  return store.put_i32(kOffsetKey, g_offset_minutes.load(std::memory_order_acquire));
}

bool store_utc_offset_minutes(NvsStore& store, int32_t minutes) {
  if (!clock_settings::valid_offset(minutes)) return false;
  if (!store.put_i32(kOffsetKey, minutes)) return false;   // memory only changes once the save succeeded
  g_offset_minutes.store(minutes, std::memory_order_release);
  return true;
}

bool format_local(char* output, size_t output_size, uint32_t epoch) {
  return clock_settings::format_local(output, output_size, epoch, utc_offset_minutes());
}

void resync_system_from_rtc() {
  if (M5.Rtc.isEnabled()) M5.Rtc.setSystemTimeFromRtc();
}

bool format_utc(char* output, size_t output_size, uint32_t epoch) {
  if (output == nullptr || output_size < 21 || !valid_epoch(epoch)) return false;
  const time_t raw = static_cast<time_t>(epoch);
  tm value{};
  if (gmtime_r(&raw, &value) == nullptr) return false;
  return snprintf(output, output_size, "%04d-%02d-%02d %02d:%02d:%02dZ",
                  value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                  value.tm_hour, value.tm_min, value.tm_sec) == 20;
}

bool self_check() {
  char value[24]{};
  return format_utc(value, sizeof(value), 1704067200) &&
         strcmp(value, "2024-01-01 00:00:00Z") == 0 &&
         !format_utc(value, sizeof(value), 0) && clock_settings::self_check();
}

}  // namespace orcsdr::time_service
