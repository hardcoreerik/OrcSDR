#include "clock_settings.hpp"

#include <cstdio>
#include <cstdlib>

namespace orcsdr::clock_settings {
namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
int64_t days_from_civil(int64_t year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + static_cast<int64_t>(day_of_era) - 719468;
}

void civil_from_days(int64_t days, int* year, int* month, int* day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
  const unsigned year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const int64_t y = static_cast<int64_t>(year_of_era) + era * 400;
  const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned mp = (5 * day_of_year + 2) / 153;
  const unsigned d = day_of_year - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  *year = static_cast<int>(y + (m <= 2));
  *month = static_cast<int>(m);
  *day = static_cast<int>(d);
}

int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

// Wraps `value` into [0, count) (so -1 becomes count-1).
int wrap(int value, int count) { return ((value % count) + count) % count; }

}  // namespace

bool is_leap_year(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int days_in_month(int year, int month) {
  static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && is_leap_year(year) ? 29 : kDays[month - 1];
}

bool valid(const LocalTime& local) {
  return local.year >= kMinYear && local.year <= kMaxYear && valid_calendar(local);
}

bool valid_calendar(const LocalTime& local) {
  return local.month >= 1 && local.month <= 12 &&
         local.day >= 1 && local.day <= days_in_month(local.year, local.month) && local.hour >= 0 &&
         local.hour <= 23 && local.minute >= 0 && local.minute <= 59 && local.second >= 0 &&
         local.second <= 59;
}

int32_t clamp_offset(int32_t minutes) {
  return minutes < kMinOffsetMinutes ? kMinOffsetMinutes : minutes > kMaxOffsetMinutes ? kMaxOffsetMinutes : minutes;
}

bool valid_offset(int32_t minutes) { return minutes >= kMinOffsetMinutes && minutes <= kMaxOffsetMinutes; }

int32_t step_offset(int32_t minutes, int direction) {
  const int32_t current = clamp_offset(minutes);
  // Land on the 15-minute grid first, so an offset set by hand (or an older value) still steps cleanly.
  const int32_t floor_step = static_cast<int32_t>(floor_div(current, kOffsetStepMinutes)) * kOffsetStepMinutes;
  if (direction > 0) return clamp_offset(floor_step + kOffsetStepMinutes);
  if (direction < 0) return clamp_offset(floor_step == current ? current - kOffsetStepMinutes : floor_step);
  return current;
}

bool utc_to_local(uint32_t utc, int32_t offset_minutes, LocalTime* local) {
  if (local == nullptr || utc < kMinUtc || utc >= kMaxUtc) return false;
  const int64_t shifted = static_cast<int64_t>(utc) + static_cast<int64_t>(clamp_offset(offset_minutes)) * 60;
  const int64_t days = floor_div(shifted, 86400);
  const int64_t seconds = shifted - days * 86400;
  LocalTime result;
  civil_from_days(days, &result.year, &result.month, &result.day);
  result.hour = static_cast<int>(seconds / 3600);
  result.minute = static_cast<int>(seconds % 3600 / 60);
  result.second = static_cast<int>(seconds % 60);
  if (!valid_calendar(result)) return false;
  *local = result;
  return true;
}

bool local_to_utc(const LocalTime& local, int32_t offset_minutes, uint32_t* utc) {
  if (utc == nullptr || !valid(local)) return false;
  const int64_t days = days_from_civil(local.year, static_cast<unsigned>(local.month), static_cast<unsigned>(local.day));
  const int64_t seconds = days * 86400 + local.hour * 3600 + local.minute * 60 + local.second -
                          static_cast<int64_t>(clamp_offset(offset_minutes)) * 60;
  if (seconds < kMinUtc || seconds >= kMaxUtc) return false;
  *utc = static_cast<uint32_t>(seconds);
  return true;
}

LocalTime adjust(const LocalTime& local, Field field, int delta) {
  LocalTime result = local;
  switch (field) {
    case Field::year:
      result.year = result.year + delta < kMinYear ? kMinYear : result.year + delta > kMaxYear ? kMaxYear : result.year + delta;
      break;
    case Field::month: result.month = wrap(result.month - 1 + delta, 12) + 1; break;
    case Field::day: {
      const int count = days_in_month(result.year, result.month);
      if (count > 0) result.day = wrap(result.day - 1 + delta, count) + 1;
      break;
    }
    case Field::hour: result.hour = wrap(result.hour + delta, 24); break;
    case Field::minute: result.minute = wrap(result.minute + delta, 60); break;
  }
  const int count = days_in_month(result.year, result.month);
  if (count > 0 && result.day > count) result.day = count;
  return result;
}

void format_offset(char* output, size_t output_size, int32_t offset_minutes) {
  if (output == nullptr || output_size == 0) return;
  const int32_t offset = clamp_offset(offset_minutes);
  const int32_t magnitude = std::abs(offset);
  snprintf(output, output_size, "UTC%c%02ld:%02ld", offset < 0 ? '-' : '+',
           static_cast<long>(magnitude / 60), static_cast<long>(magnitude % 60));
}

void format_offset_short(char* output, size_t output_size, int32_t offset_minutes) {
  if (output == nullptr || output_size == 0) return;
  const int32_t offset = clamp_offset(offset_minutes);
  if (offset == 0) {
    snprintf(output, output_size, "UTC");
    return;
  }
  const int32_t magnitude = std::abs(offset);
  if (magnitude % 60 == 0)
    snprintf(output, output_size, "UTC%c%ld", offset < 0 ? '-' : '+', static_cast<long>(magnitude / 60));
  else
    snprintf(output, output_size, "UTC%c%ld:%02ld", offset < 0 ? '-' : '+', static_cast<long>(magnitude / 60),
             static_cast<long>(magnitude % 60));
}

bool format_local(char* output, size_t output_size, uint32_t utc, int32_t offset_minutes) {
  LocalTime local;
  if (output == nullptr || output_size < 20 || !utc_to_local(utc, offset_minutes, &local)) return false;
  return snprintf(output, output_size, "%04d-%02d-%02d %02d:%02d:%02d", local.year, local.month, local.day,
                  local.hour, local.minute, local.second) == 19;
}

bool self_check() {
  LocalTime local;
  uint32_t utc = 0;
  char text[24]{};
  return utc_to_local(kMinUtc, 0, &local) && local.year == 2024 && local.month == 1 && local.day == 1 &&
         local_to_utc(local, 0, &utc) && utc == kMinUtc && format_local(text, sizeof(text), kMinUtc, 0) &&
         text[0] == '2' && days_in_month(2028, 2) == 29 && days_in_month(2026, 2) == 28 &&
         step_offset(0, 1) == 15 && step_offset(kMaxOffsetMinutes, 1) == kMaxOffsetMinutes &&
         // a non-zero offset round-trips, and the UTC bounds convert for any offset
         utc_to_local(1791404100u, -7 * 60, &local) && local.hour == 13 && local.minute == 15 &&
         local_to_utc(local, -7 * 60, &utc) && utc == 1791404100u && utc_to_local(kMinUtc, -15, &local) &&
         utc_to_local(kMaxUtc - 1, 15, &local);
}

}  // namespace orcsdr::clock_settings
