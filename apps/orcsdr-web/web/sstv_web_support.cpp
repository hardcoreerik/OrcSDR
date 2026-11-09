// What the SSTV dashboard links against that only makes sense on the device: the SD archive worker and the RTC-backed time service.
// A browser has neither, so the archive reports "nothing saved" (the dashboard shows its RAM gallery) and the clock is the page's.
#include <emscripten.h>

#include <cstdio>
#include <ctime>

#include "sstv_archive.hpp"
#include "time_service.hpp"

namespace orcsdr::sstv_archive {

const char* error_name(Error error) {
  switch (error) {
    case Error::no_card: return "NO CARD";
    case Error::no_space: return "NO SPACE";
    case Error::write_error: return "WRITE ERROR";
    case Error::rename_error: return "RENAME ERROR";
    case Error::index_error: return "INDEX ERROR";
    case Error::too_few_rows: return "TOO FEW ROWS";
    case Error::queue_full: return "QUEUE FULL";
    case Error::shutdown: return "SHUTDOWN";
    default: return "NONE";
  }
}

const char* state_name(JobState state) {
  switch (state) {
    case JobState::pending: return "PENDING";
    case JobState::saving: return "SAVING";
    case JobState::saved: return "SAVED";
    case JobState::failed: return "FAILED";
    case JobState::skipped: return "SKIPPED";
    default: return "NONE";
  }
}

Stats stats() { return Stats{}; }
bool result_for(uint32_t, Result*) { return false; }

}  // namespace orcsdr::sstv_archive

namespace orcsdr::time_service {

namespace {
uint32_t g_utc = 0;
double g_at = 0;
}

void set_page_clock(uint32_t epoch_seconds) {
  g_utc = epoch_seconds;
  g_at = emscripten_get_now();
}

Snapshot now() {
  Snapshot s;
  s.uptime_ms = (uint32_t)emscripten_get_now();
  s.wallclock_valid = g_utc != 0;
  s.utc = g_utc ? g_utc + (uint32_t)((emscripten_get_now() - g_at) / 1000.0) : 0;
  return s;
}

bool format_utc(char* output, size_t output_size, uint32_t epoch) {
  const time_t t = (time_t)epoch;
  struct tm tm;
  if (!gmtime_r(&t, &tm)) return false;
  snprintf(output, output_size, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return true;
}

bool format_local(char* output, size_t output_size, uint32_t epoch) { return format_utc(output, output_size, epoch); }

}  // namespace orcsdr::time_service
