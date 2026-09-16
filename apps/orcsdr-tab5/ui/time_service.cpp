#include "time_service.hpp"

#include <M5Unified.h>

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

bool valid_epoch(time_t value) { return value >= kMinUtc && value < kMaxUtc; }

}  // namespace

void initialize(bool previously_established) {
  m5::rtc_datetime_t value{};
  const bool calendar_valid = M5.Rtc.isEnabled() && M5.Rtc.getDateTime(&value) &&
                              value.date.year >= 2024 && value.date.year <= 2099;
  if (previously_established && calendar_valid) M5.Rtc.setSystemTimeFromRtc();
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
         !format_utc(value, sizeof(value), 0);
}

}  // namespace orcsdr::time_service
