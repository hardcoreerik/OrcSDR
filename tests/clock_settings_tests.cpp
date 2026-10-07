#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clock_settings.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

using namespace orcsdr::clock_settings;

LocalTime at(int year, int month, int day, int hour, int minute, int second = 0) {
  LocalTime value;
  value.year = year; value.month = month; value.day = day;
  value.hour = hour; value.minute = minute; value.second = second;
  return value;
}

void test_calendar() {
  CHECK(is_leap_year(2028) && !is_leap_year(2026) && !is_leap_year(2100) && is_leap_year(2000));
  CHECK(days_in_month(2028, 2) == 29 && days_in_month(2026, 2) == 28 && days_in_month(2026, 4) == 30);
  CHECK(days_in_month(2026, 0) == 0 && days_in_month(2026, 13) == 0);
  CHECK(valid(at(2026, 10, 7, 12, 30)));
  CHECK(!valid(at(2026, 2, 29, 0, 0)));
  CHECK(valid(at(2028, 2, 29, 23, 59, 59)));
  CHECK(!valid(at(2023, 12, 31, 23, 59)) && !valid(at(2100, 1, 1, 0, 0)));
  CHECK(!valid(at(2026, 13, 1, 0, 0)) && !valid(at(2026, 1, 1, 24, 0)) && !valid(at(2026, 1, 1, 0, 60)));
}

void test_known_epochs() {
  LocalTime local;
  CHECK(utc_to_local(1704067200u, 0, &local));   // 2024-01-01 00:00:00 UTC
  CHECK(local.year == 2024 && local.month == 1 && local.day == 1 && local.hour == 0 && local.minute == 0);
  CHECK(utc_to_local(1767225600u, 0, &local));   // 2026-01-01 00:00:00 UTC
  CHECK(local.year == 2026 && local.month == 1 && local.day == 1);
  CHECK(utc_to_local(1835481600u, 0, &local));   // 2028-03-01 00:00:00 UTC, the day after the leap day
  CHECK(local.year == 2028 && local.month == 3 && local.day == 1);
  CHECK(utc_to_local(1835481600u - 1, 0, &local));
  CHECK(local.year == 2028 && local.month == 2 && local.day == 29 && local.hour == 23 && local.second == 59);
  uint32_t utc = 0;
  CHECK(local_to_utc(at(2026, 1, 1, 0, 0), 0, &utc) && utc == 1767225600u);
}

void test_offsets() {
  LocalTime local;
  uint32_t utc = 0;
  // 2026-10-07 20:15:00 UTC is 13:15 in Oregon (UTC-7) and the next morning at 01:45 in Nepal (UTC+5:45).
  CHECK(local_to_utc(at(2026, 10, 7, 20, 15), 0, &utc));
  CHECK(utc_to_local(utc, -7 * 60, &local) && local.day == 7 && local.hour == 13 && local.minute == 15);
  CHECK(utc_to_local(utc, 5 * 60 + 45, &local) && local.day == 8 && local.hour == 2 && local.minute == 0);
  CHECK(local_to_utc(at(2026, 10, 7, 13, 15), -7 * 60, &utc) && utc == 1791404100u);
  // The same instant read back at the same offset round-trips for every supported step.
  for (int32_t offset = kMinOffsetMinutes; offset <= kMaxOffsetMinutes; offset += kOffsetStepMinutes) {
    uint32_t back = 0;
    CHECK(utc_to_local(1791404100u, offset, &local) && local_to_utc(local, offset, &back) && back == 1791404100u);
  }
  // A new year arrives earlier in UTC+14 than in UTC.
  CHECK(utc_to_local(1798761600u - 1, 14 * 60, &local) && local.year == 2027 && local.month == 1 && local.day == 1);
  CHECK(utc_to_local(1798761600u - 1, 0, &local) && local.year == 2026 && local.month == 12 && local.day == 31);
}

void test_range_limits() {
  LocalTime local;
  uint32_t utc = 0;
  CHECK(!utc_to_local(1704067199u, 0, &local));        // before 2024
  CHECK(!utc_to_local(4102444800u, 0, &local));        // 2100 and later
  CHECK(!local_to_utc(at(2023, 12, 31, 23, 59), 0, &utc));
  CHECK(!local_to_utc(at(2026, 2, 30, 0, 0), 0, &utc));
  // 2024-01-01 00:30 at UTC+1 is before the supported start in UTC.
  CHECK(!local_to_utc(at(2024, 1, 1, 0, 30), 60, &utc));
  CHECK(local_to_utc(at(2024, 1, 1, 1, 0), 60, &utc) && utc == 1704067200u);
  CHECK(!utc_to_local(0, 0, nullptr) && !local_to_utc(at(2026, 1, 1, 0, 0), 0, nullptr));
  // The UTC bounds themselves convert for every offset, even when the local year is 2023 or 2100.
  CHECK(utc_to_local(1704067200u, -15, &local) && local.year == 2023 && local.month == 12 && local.day == 31);
  CHECK(utc_to_local(4102444799u, 14 * 60, &local) && local.year == 2100 && local.month == 1 && local.day == 1);
  CHECK(!valid(local) && valid_calendar(local));
}

void test_offset_steps() {
  CHECK(clamp_offset(-9999) == kMinOffsetMinutes && clamp_offset(9999) == kMaxOffsetMinutes);
  CHECK(step_offset(0, 1) == 15 && step_offset(0, -1) == -15);
  CHECK(step_offset(-420, 1) == -405 && step_offset(-420, -1) == -435);
  CHECK(step_offset(kMaxOffsetMinutes, 1) == kMaxOffsetMinutes);
  CHECK(step_offset(kMinOffsetMinutes, -1) == kMinOffsetMinutes);
  CHECK(step_offset(7, 1) == 15 && step_offset(7, -1) == 0);          // an off-grid value lands on the grid
  CHECK(step_offset(-7, 1) == 0 && step_offset(-7, -1) == -15);
  CHECK(step_offset(60, 0) == 60);
  { LocalTime zero{}; zero.month = 0; (void)adjust(zero, Field::day, 1); }   // no division by zero
  CHECK(valid_offset(-720) && valid_offset(840) && !valid_offset(-721) && !valid_offset(841));
}

void test_adjust() {
  // The day is pulled back into a shorter month.
  LocalTime value = adjust(at(2026, 1, 31, 8, 0), Field::month, +1);
  CHECK(value.month == 2 && value.day == 28);
  value = adjust(at(2028, 1, 31, 8, 0), Field::month, +1);
  CHECK(value.month == 2 && value.day == 29);
  value = adjust(at(2028, 2, 29, 8, 0), Field::year, +1);
  CHECK(value.year == 2029 && value.month == 2 && value.day == 28);
  // Months, days, hours and minutes wrap; the year stops at its limits.
  CHECK(adjust(at(2026, 12, 5, 0, 0), Field::month, +1).month == 1);
  CHECK(adjust(at(2026, 1, 5, 0, 0), Field::month, -1).month == 12);
  CHECK(adjust(at(2026, 4, 30, 0, 0), Field::day, +1).day == 1);
  CHECK(adjust(at(2026, 4, 1, 0, 0), Field::day, -1).day == 30);
  CHECK(adjust(at(2026, 4, 1, 23, 0), Field::hour, +1).hour == 0);
  CHECK(adjust(at(2026, 4, 1, 0, 0), Field::hour, -1).hour == 23);
  CHECK(adjust(at(2026, 4, 1, 0, 59), Field::minute, +1).minute == 0);
  CHECK(adjust(at(2026, 4, 1, 0, 0), Field::minute, -1).minute == 59);
  CHECK(adjust(at(2099, 4, 1, 0, 0), Field::year, +1).year == 2099);
  CHECK(adjust(at(2024, 4, 1, 0, 0), Field::year, -1).year == 2024);
  // Month wrap does not touch the year (the year has its own control).
  CHECK(adjust(at(2026, 12, 5, 0, 0), Field::month, +1).year == 2026);
  // Everything adjust() returns is a valid time.
  for (int field = 0; field < 5; ++field)
    for (int delta = -1; delta <= 1; delta += 2)
      CHECK(valid(adjust(at(2028, 2, 29, 23, 59), static_cast<Field>(field), delta)));
}

void test_formatting() {
  char text[32];
  format_offset(text, sizeof(text), -7 * 60);       CHECK(std::strcmp(text, "UTC-07:00") == 0);
  format_offset(text, sizeof(text), 5 * 60 + 45);   CHECK(std::strcmp(text, "UTC+05:45") == 0);
  format_offset(text, sizeof(text), 0);             CHECK(std::strcmp(text, "UTC+00:00") == 0);
  format_offset(text, sizeof(text), -9999);         CHECK(std::strcmp(text, "UTC-12:00") == 0);
  format_offset_short(text, sizeof(text), 0);       CHECK(std::strcmp(text, "UTC") == 0);
  format_offset_short(text, sizeof(text), -7 * 60); CHECK(std::strcmp(text, "UTC-7") == 0);
  format_offset_short(text, sizeof(text), 330);     CHECK(std::strcmp(text, "UTC+5:30") == 0);
  format_offset_short(text, sizeof(text), -570);    CHECK(std::strcmp(text, "UTC-9:30") == 0);
  CHECK(format_local(text, sizeof(text), 1791404100u, -7 * 60) && std::strcmp(text, "2026-10-07 13:15:00") == 0);
  CHECK(!format_local(text, 19, 1791404100u, 0));    // needs room for the terminator
  CHECK(!format_local(text, sizeof(text), 0, 0));
  char tiny[5];
  format_offset_short(tiny, sizeof(tiny), -7 * 60);  // truncates, never overruns
  CHECK(std::strlen(tiny) < sizeof(tiny));
}

}  // namespace

int main() {
  CHECK(orcsdr::clock_settings::self_check());
  test_calendar();
  test_known_epochs();
  test_offsets();
  test_range_limits();
  test_offset_steps();
  test_adjust();
  test_formatting();
  std::printf("CLOCK_SETTINGS_TESTS pass\n");
  return 0;
}
