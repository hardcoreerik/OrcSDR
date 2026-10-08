#include "ft8_adif.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::ft8 {
namespace {

struct BandRange {
  const char* name;
  uint32_t low_hz;
  uint32_t high_hz;
};

constexpr BandRange kBands[] = {
    {"160m", 1800000u, 2000000u},   {"80m", 3500000u, 4000000u},    {"60m", 5060000u, 5450000u},
    {"40m", 7000000u, 7300000u},    {"30m", 10100000u, 10150000u},  {"20m", 14000000u, 14350000u},
    {"17m", 18068000u, 18168000u},  {"15m", 21000000u, 21450000u},  {"12m", 24890000u, 24990000u},
    {"10m", 28000000u, 29700000u},  {"6m", 50000000u, 54000000u},   {"2m", 144000000u, 148000000u},
    {"70cm", 420000000u, 450000000u},
};

// Days since 1970-01-01 to a civil date (proleptic Gregorian).
void civil_from_days(int64_t z, int* year, unsigned* month, unsigned* day) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *day = doy - (153 * mp + 2) / 5 + 1;
  *month = mp < 10 ? mp + 3 : mp - 9;
  *year = static_cast<int>(y + (*month <= 2 ? 1 : 0));
}

// Appends "<NAME:len>value " and keeps `used` within `capacity`.
bool field(char* out, size_t capacity, size_t* used, const char* name, const char* value) {
  const size_t len = std::strlen(value);
  const int n = std::snprintf(out + *used, capacity - *used, "<%s:%u>%s ", name, static_cast<unsigned>(len), value);
  if (n < 0 || static_cast<size_t>(n) >= capacity - *used) return false;
  *used += static_cast<size_t>(n);
  return true;
}

}  // namespace

const char* adif_band(uint32_t hz) {
  for (const auto& b : kBands)
    if (hz >= b.low_hz && hz <= b.high_hz) return b.name;
  return nullptr;
}

size_t adif_header(char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  static const char kText[] =
      "OrcSDR heard-stations log (receive-only; these are stations heard, not contacts)\n"
      "<ADIF_VER:5>3.1.4 <PROGRAMID:6>OrcSDR <EOH>\n";
  const size_t len = sizeof(kText) - 1;
  if (len >= capacity) return 0;
  std::memcpy(out, kText, len + 1);
  return len;
}

size_t adif_heard_record(const Decode& decode, uint32_t dial_hz, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0 || decode.callsign[0] == '\0') return 0;
  const char* mode_name = nullptr;
  const char* submode = nullptr;
  if (decode.mode == DigitalMode::ft8) {
    mode_name = "FT8";
  } else if (decode.mode == DigitalMode::ft4) {
    mode_name = "MFSK";
    submode = "FT4";
  } else {
    return 0;
  }
  const uint32_t rf_hz = dial_hz + decode.audio_hz;
  const char* band = adif_band(rf_hz);
  if (band == nullptr) return 0;

  const int64_t days = static_cast<int64_t>(decode.utc_epoch / 86400u);
  const uint32_t seconds = decode.utc_epoch % 86400u;
  int year = 0;
  unsigned month = 0, day = 0;
  civil_from_days(days, &year, &month, &day);

  char date[40], time[40], freq[40];
  std::snprintf(date, sizeof(date), "%04d%02u%02u", year, month, day);
  std::snprintf(time, sizeof(time), "%02u%02u%02u", static_cast<unsigned>(seconds / 3600u), static_cast<unsigned>((seconds / 60u) % 60u),
                static_cast<unsigned>(seconds % 60u));
  std::snprintf(freq, sizeof(freq), "%u.%06u", static_cast<unsigned>(rf_hz / 1000000u), static_cast<unsigned>(rf_hz % 1000000u));

  size_t used = 0;
  out[0] = '\0';
  if (!field(out, capacity, &used, "CALL", decode.callsign) || !field(out, capacity, &used, "QSO_DATE", date) ||
      !field(out, capacity, &used, "TIME_ON", time) || !field(out, capacity, &used, "BAND", band) ||
      !field(out, capacity, &used, "FREQ", freq) || !field(out, capacity, &used, "MODE", mode_name))
    return 0;
  if (submode != nullptr && !field(out, capacity, &used, "SUBMODE", submode)) return 0;
  if (maidenhead_valid(decode.grid) && !field(out, capacity, &used, "GRIDSQUARE", decode.grid)) return 0;
  if (!field(out, capacity, &used, "COMMENT", "Heard only (OrcSDR, receive-only)")) return 0;
  const char eor[] = "<EOR>\n";
  if (used + sizeof(eor) > capacity) return 0;
  std::memcpy(out + used, eor, sizeof(eor));
  return used + sizeof(eor) - 1;
}

bool adif_self_check() {
  Decode d{};
  std::snprintf(d.callsign, sizeof(d.callsign), "KK6KC");
  std::snprintf(d.grid, sizeof(d.grid), "DM03");
  d.utc_epoch = 1791440160u;  // 2026-10-08 06:16:00 UTC
  d.audio_hz = 1739;
  char record[256];
  const size_t n = adif_heard_record(d, 7074000u, record, sizeof(record));
  char tiny[8];
  return n > 0 && std::strstr(record, "<CALL:5>KK6KC") != nullptr &&
         std::strstr(record, "<QSO_DATE:8>20261008") != nullptr && std::strstr(record, "<TIME_ON:6>061600") != nullptr &&
         std::strstr(record, "<BAND:3>40m") != nullptr && std::strstr(record, "<FREQ:8>7.075739") != nullptr &&
         std::strstr(record, "<MODE:3>FT8") != nullptr && std::strstr(record, "<GRIDSQUARE:4>DM03") != nullptr &&
         std::strstr(record, "<EOR>") != nullptr && adif_heard_record(d, 7074000u, tiny, sizeof(tiny)) == 0 &&
         adif_heard_record(d, 1000000u, record, sizeof(record)) == 0 && adif_band(144174000u) != nullptr;
}

}  // namespace orcsdr::ft8
