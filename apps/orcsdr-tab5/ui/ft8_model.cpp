#include "ft8_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orcsdr::ft8 {
namespace {

constexpr BandPreset kBands[] = {
    {"160m", 1840000, true},   {"80m", 3573000, true},
    {"60m", 5357000, false},   {"40m", 7074000, true},
    {"30m", 10136000, true},   {"20m", 14074000, true},
    {"17m", 18100000, true},   {"15m", 21074000, true},
    {"12m", 24915000, true},   {"10m", 28074000, true},
    {"6m", 50313000, true},    {"2m", 144174000, false},
};

bool token_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '-';
}

void copy_token(const char* begin, size_t length, char* out, size_t out_size) {
  if (out == nullptr || out_size == 0) return;
  const size_t copied = std::min(length, out_size - 1);
  std::memcpy(out, begin, copied);
  out[copied] = '\0';
}

bool is_grid4(const char* value) {
  if (value == nullptr || std::strlen(value) != 4) return false;
  return std::toupper(static_cast<unsigned char>(value[0])) >= 'A' &&
         std::toupper(static_cast<unsigned char>(value[0])) <= 'R' &&
         std::toupper(static_cast<unsigned char>(value[1])) >= 'A' &&
         std::toupper(static_cast<unsigned char>(value[1])) <= 'R' &&
         std::isdigit(static_cast<unsigned char>(value[2])) &&
         std::isdigit(static_cast<unsigned char>(value[3]));
}

}  // namespace

size_t band_count() { return sizeof(kBands) / sizeof(kBands[0]); }

const BandPreset* band(size_t index) {
  return index < band_count() ? &kBands[index] : nullptr;
}

size_t nearest_band(uint32_t dial_hz) {
  size_t best = 0;
  uint32_t best_delta = UINT32_MAX;
  for (size_t i = 0; i < band_count(); ++i) {
    const uint32_t delta = dial_hz > kBands[i].dial_hz
                               ? dial_hz - kBands[i].dial_hz
                               : kBands[i].dial_hz - dial_hz;
    if (delta < best_delta) {
      best_delta = delta;
      best = i;
    }
  }
  return best;
}

SlotClock slot_clock(uint64_t utc_ms) {
  SlotClock result{};
  result.slot_index = static_cast<uint32_t>(utc_ms / kSlotMs);
  result.elapsed_ms = static_cast<uint32_t>(utc_ms % kSlotMs);
  result.remaining_ms = kSlotMs - result.elapsed_ms;
  const uint32_t second_in_minute = static_cast<uint32_t>((utc_ms / 1000u) % 60u);
  result.first_half_minute = second_in_minute < 30;
  return result;
}

DecodeKind classify_message(const char* message) {
  if (message == nullptr || message[0] == '\0') return DecodeKind::unknown;
  while (*message == ' ') ++message;
  if (std::strncmp(message, "CQ ", 3) == 0 || std::strcmp(message, "CQ") == 0)
    return DecodeKind::cq;
  if (std::strstr(message, " RR73") != nullptr ||
      std::strstr(message, " RRR") != nullptr ||
      std::strstr(message, " 73") != nullptr ||
      std::strstr(message, " R+") != nullptr ||
      std::strstr(message, " R-") != nullptr)
    return DecodeKind::qso;
  int spaces = 0;
  for (const char* p = message; *p; ++p) spaces += *p == ' ';
  return spaces >= 2 ? DecodeKind::qso : DecodeKind::free_text;
}

bool maidenhead_valid(const char* locator) {
  if (locator == nullptr) return false;
  const size_t n = std::strlen(locator);
  if (n != 4 && n != 6 && n != 8) return false;
  const char a = static_cast<char>(std::toupper(static_cast<unsigned char>(locator[0])));
  const char b = static_cast<char>(std::toupper(static_cast<unsigned char>(locator[1])));
  if (a < 'A' || a > 'R' || b < 'A' || b > 'R') return false;
  if (!std::isdigit(static_cast<unsigned char>(locator[2])) ||
      !std::isdigit(static_cast<unsigned char>(locator[3]))) return false;
  if (n >= 6) {
    const char e = static_cast<char>(std::toupper(static_cast<unsigned char>(locator[4])));
    const char f = static_cast<char>(std::toupper(static_cast<unsigned char>(locator[5])));
    if (e < 'A' || e > 'X' || f < 'A' || f > 'X') return false;
  }
  if (n == 8) {
    if (!std::isdigit(static_cast<unsigned char>(locator[6])) ||
        !std::isdigit(static_cast<unsigned char>(locator[7]))) return false;
  }
  return true;
}

bool maidenhead_center(const char* locator, GeoPoint* out) {
  if (out == nullptr || !maidenhead_valid(locator)) return false;
  const size_t n = std::strlen(locator);
  const int field_lon = std::toupper(static_cast<unsigned char>(locator[0])) - 'A';
  const int field_lat = std::toupper(static_cast<unsigned char>(locator[1])) - 'A';
  const int square_lon = locator[2] - '0';
  const int square_lat = locator[3] - '0';

  double lon = -180.0 + field_lon * 20.0 + square_lon * 2.0;
  double lat = -90.0 + field_lat * 10.0 + square_lat * 1.0;
  double cell_lon = 2.0;
  double cell_lat = 1.0;

  if (n >= 6) {
    const int sub_lon = std::toupper(static_cast<unsigned char>(locator[4])) - 'A';
    const int sub_lat = std::toupper(static_cast<unsigned char>(locator[5])) - 'A';
    cell_lon /= 24.0;
    cell_lat /= 24.0;
    lon += sub_lon * cell_lon;
    lat += sub_lat * cell_lat;
  }
  if (n == 8) {
    cell_lon /= 10.0;
    cell_lat /= 10.0;
    lon += (locator[6] - '0') * cell_lon;
    lat += (locator[7] - '0') * cell_lat;
  }
  out->longitude = static_cast<float>(lon + cell_lon / 2.0);
  out->latitude = static_cast<float>(lat + cell_lat / 2.0);
  out->precision = static_cast<uint8_t>(n);
  return std::isfinite(out->latitude) && std::isfinite(out->longitude);
}

bool parse_cq_fields(const char* message, char* callsign, size_t callsign_size,
                     char* grid, size_t grid_size) {
  if (callsign && callsign_size) callsign[0] = '\0';
  if (grid && grid_size) grid[0] = '\0';
  if (message == nullptr) return false;
  while (*message == ' ') ++message;
  if (std::strncmp(message, "CQ", 2) != 0) return false;

  const char* tokens[6]{};
  size_t lengths[6]{};
  size_t count = 0;
  const char* p = message;
  while (*p && count < 6) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* start = p;
    while (*p && *p != ' ') ++p;
    tokens[count] = start;
    lengths[count] = static_cast<size_t>(p - start);
    ++count;
  }
  if (count < 2) return false;

  size_t grid_index = SIZE_MAX;
  for (size_t i = 1; i < count; ++i) {
    char candidate[9]{};
    copy_token(tokens[i], lengths[i], candidate, sizeof(candidate));
    if (is_grid4(candidate) || maidenhead_valid(candidate)) grid_index = i;
  }
  size_t call_index = grid_index != SIZE_MAX && grid_index > 1 ? grid_index - 1 : count - 1;
  if (grid_index == SIZE_MAX && count >= 3 && lengths[1] <= 3) call_index = 2;
  if (call_index == 0 || call_index >= count) return false;

  for (size_t i = 0; i < lengths[call_index]; ++i)
    if (!token_char(tokens[call_index][i])) return false;
  copy_token(tokens[call_index], lengths[call_index], callsign, callsign_size);
  if (grid_index != SIZE_MAX)
    copy_token(tokens[grid_index], lengths[grid_index], grid, grid_size);
  return callsign != nullptr && callsign[0] != '\0';
}

const char* kind_name(DecodeKind kind) {
  switch (kind) {
    case DecodeKind::cq: return "CQ";
    case DecodeKind::qso: return "QSO";
    case DecodeKind::free_text: return "TEXT";
    case DecodeKind::unknown: return "?";
  }
  return "?";
}

void DecodeStore::clear() {
  size_ = 0;
  for (auto& record : records_) record = Decode{};
}

void DecodeStore::append(const Decode& decode) {
  if (size_ < kDecodeCapacity) {
    records_[size_++] = decode;
    return;
  }
  std::memmove(records_, records_ + 1, sizeof(records_[0]) * (kDecodeCapacity - 1));
  records_[kDecodeCapacity - 1] = decode;
}

const Decode* DecodeStore::newest(size_t offset) const {
  return offset < size_ ? &records_[size_ - 1 - offset] : nullptr;
}

size_t DecodeStore::unique_calls() const {
  size_t count = 0;
  for (size_t i = 0; i < size_; ++i) {
    if (!records_[i].callsign[0]) continue;
    bool seen = false;
    for (size_t j = 0; j < i; ++j)
      if (std::strcmp(records_[j].callsign, records_[i].callsign) == 0) {
        seen = true;
        break;
      }
    if (!seen) ++count;
  }
  return count;
}

size_t DecodeStore::cq_count() const {
  size_t count = 0;
  for (size_t i = 0; i < size_; ++i) count += records_[i].kind == DecodeKind::cq;
  return count;
}

size_t DecodeStore::grid_count() const {
  size_t count = 0;
  for (size_t i = 0; i < size_; ++i) count += maidenhead_valid(records_[i].grid);
  return count;
}

bool self_check() {
  const auto first = slot_clock(0);
  const auto boundary = slot_clock(15000);
  const auto late = slot_clock(44999);
  GeoPoint fn42{};
  char call[16]{}, grid[9]{};
  return band_count() >= 10 && band(5) && band(5)->dial_hz == 14074000 &&
         nearest_band(14074100) == 5 && first.elapsed_ms == 0 &&
         first.remaining_ms == 15000 && boundary.elapsed_ms == 0 &&
         boundary.slot_index == 1 && late.elapsed_ms == 14999 &&
         !late.first_half_minute && maidenhead_center("FN42", &fn42) &&
         fn42.latitude > 41.0f && fn42.latitude < 43.0f &&
         fn42.longitude < -70.0f && fn42.longitude > -73.0f &&
         parse_cq_fields("CQ K1ABC FN42", call, sizeof(call), grid, sizeof(grid)) &&
         std::strcmp(call, "K1ABC") == 0 && std::strcmp(grid, "FN42") == 0 &&
         classify_message("CQ DX DL1ABC JO62") == DecodeKind::cq;
}

}  // namespace orcsdr::ft8
