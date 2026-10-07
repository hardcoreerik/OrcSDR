#pragma once

#include <cstddef>
#include <cstdint>

// Calendar arithmetic for the on-device clock setup (Settings > System > SET CLOCK, and the setup wizard). Pure
// logic with no hardware, display or RTOS: the hardware RTC always holds UTC, and a UTC offset (minutes east of
// UTC) turns it into the local time the person types and reads. Host-tested.
namespace orcsdr::clock_settings {

constexpr int32_t kMinOffsetMinutes = -12 * 60;   // UTC-12:00
constexpr int32_t kMaxOffsetMinutes = 14 * 60;    // UTC+14:00
constexpr int32_t kOffsetStepMinutes = 15;        // covers the :15, :30 and :45 zones
constexpr uint32_t kMinUtc = 1704067200;          // 2024-01-01 00:00:00, as time_service accepts
constexpr uint32_t kMaxUtc = 4102444800;          // 2100-01-01 00:00:00 (exclusive)
constexpr int kMinYear = 2024;
constexpr int kMaxYear = 2099;

struct LocalTime {
  int year = 2026;
  int month = 1;
  int day = 1;
  int hour = 12;
  int minute = 0;
  int second = 0;
};

enum class Field : uint8_t { year, month, day, hour, minute };

bool is_leap_year(int year);
int days_in_month(int year, int month);

// A real calendar date and time inside the supported years.
bool valid(const LocalTime& local);
// The same without the year limits (a UTC instant at the edge of the range can be 2023 or 2100 locally).
bool valid_calendar(const LocalTime& local);

int32_t clamp_offset(int32_t minutes);
// One 15-minute step in `direction` (+1 or -1), kept inside the supported range.
int32_t step_offset(int32_t minutes, int direction);

// UTC <-> local time at `offset_minutes`. Both fail outside the supported range.
bool utc_to_local(uint32_t utc, int32_t offset_minutes, LocalTime* local);
bool local_to_utc(const LocalTime& local, int32_t offset_minutes, uint32_t* utc);

// One step of a touchscreen +/- button: months, days, hours and minutes wrap, the year stops at its limits,
// and the day is pulled back into the month ("31 Jan" plus a month is "28 or 29 Feb").
LocalTime adjust(const LocalTime& local, Field field, int delta);

// "UTC-07:00" (always with sign and minutes) and the short "UTC", "UTC-7", "UTC+5:30" for tight spaces.
void format_offset(char* output, size_t output_size, int32_t offset_minutes);
void format_offset_short(char* output, size_t output_size, int32_t offset_minutes);

// "YYYY-MM-DD HH:MM:SS" at `offset_minutes` (needs output_size >= 20).
bool format_local(char* output, size_t output_size, uint32_t utc, int32_t offset_minutes);

bool self_check();

}  // namespace orcsdr::clock_settings
