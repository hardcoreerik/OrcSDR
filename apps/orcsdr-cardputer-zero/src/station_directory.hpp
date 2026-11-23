#pragma once
// Reader for the compact US AM/FM station index built by
// tools/build_station_index.py from the FCC LMS snapshot.
//
// The table is fixed-width and sorted by frequency so the UI can binary-search
// a tuning position, and neighbours can be scanned by distance without touching
// the 9 MB source NDJSON.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace orc::zero::stations {

inline constexpr uint8_t kServiceAm = 0;
inline constexpr uint8_t kServiceFm = 1;
inline constexpr size_t kCallChars = 8;
inline constexpr size_t kCityChars = 20;
inline constexpr size_t kRegionChars = 3;

struct Record {
  uint32_t frequency_hz = 0;
  uint8_t service = kServiceFm;
  uint8_t licensed = 0;  // bit0 LICEN, bit1 licensed-silent, bit2 LICRP, bit3 LICSU
  char call[kCallChars + 1]{};
  char city[kCityChars + 1]{};
  char region[kRegionChars + 1]{};
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;

  bool silent() const { return (licensed & 0x02) != 0; }
  double latitude() const { return latitude_e7 / 1e7; }
  double longitude() const { return longitude_e7 / 1e7; }
  bool has_position() const { return latitude_e7 != 0 || longitude_e7 != 0; }
};

class Directory {
 public:
  bool load(const std::string& path, std::string& error) {
    records_.clear();
    provenance_.clear();
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
      error = "station index not found: " + path;
      return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length < 0) {
      std::fclose(file);
      error = "station index unreadable";
      return false;
    }
    const size_t byte_count = size_t(length);
    std::vector<uint8_t> bytes(byte_count);
    const size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (got != bytes.size()) {
      error = "station index short read";
      return false;
    }
    return parse(bytes, error);
  }

  bool parse(const std::vector<uint8_t>& bytes, std::string& error) {
    constexpr size_t kMagic = 7;
    constexpr size_t kHeader = kMagic + 12 + 120;
    constexpr size_t kRecord = 45;
    if (bytes.size() < kHeader || std::memcmp(bytes.data(), "ORCSTN\x01", kMagic) != 0) {
      error = "not a station index";
      return false;
    }
    const auto* header = bytes.data() + kMagic;
    uint32_t version = 0, count = 0, record_size = 0;
    std::memcpy(&version, header + 0, 4);
    std::memcpy(&count, header + 4, 4);
    std::memcpy(&record_size, header + 8, 4);
    if (version != 1 || record_size != kRecord) {
      error = "unsupported station index version";
      return false;
    }
    if (bytes.size() < kHeader + size_t(count) * kRecord) {
      error = "station index truncated";
      return false;
    }
    provenance_.assign(reinterpret_cast<const char*>(header + 12), 120);
    provenance_.resize(provenance_.find('\0') == std::string::npos ? provenance_.size()
                                                                  : provenance_.find('\0'));

    records_.resize(count);
    const uint8_t* cursor = bytes.data() + kHeader;
    for (uint32_t n = 0; n < count; ++n, cursor += kRecord) {
      Record& record = records_[n];
      std::memcpy(&record.frequency_hz, cursor + 0, 4);
      record.service = cursor[4];
      record.licensed = cursor[5];
      std::memcpy(record.call, cursor + 6, kCallChars);
      std::memcpy(record.city, cursor + 14, kCityChars);
      std::memcpy(record.region, cursor + 34, kRegionChars);
      std::memcpy(&record.latitude_e7, cursor + 37, 4);
      std::memcpy(&record.longitude_e7, cursor + 41, 4);
      record.call[kCallChars] = '\0';
      record.city[kCityChars] = '\0';
      record.region[kRegionChars] = '\0';
    }
    return true;
  }

  size_t size() const { return records_.size(); }
  bool empty() const { return records_.empty(); }
  const Record& at(size_t index) const { return records_[index]; }
  const std::string& provenance() const { return provenance_; }

  // First record at or after `frequency_hz`, which is where a tuning position
  // lands in the sorted table.
  size_t lower_bound(uint32_t frequency_hz) const {
    return size_t(std::lower_bound(records_.begin(), records_.end(), frequency_hz,
                                   [](const Record& record, uint32_t hz) {
                                     return record.frequency_hz < hz;
                                   }) -
                  records_.begin());
  }

  // Stations within `window_hz` of a frequency, in tuning order. `service` < 0
  // accepts both bands.
  std::vector<size_t> near_frequency(uint32_t frequency_hz, uint32_t window_hz,
                                     int service = -1) const {
    std::vector<size_t> found;
    const uint32_t low = frequency_hz > window_hz ? frequency_hz - window_hz : 0;
    for (size_t index = lower_bound(low); index < records_.size(); ++index) {
      const Record& record = records_[index];
      if (record.frequency_hz > frequency_hz + window_hz) break;
      if (service >= 0 && record.service != uint8_t(service)) continue;
      found.push_back(index);
    }
    return found;
  }

  // Nearest stations to a position, capped at `wanted`. Distance is planar over
  // a local tangent, which is accurate enough at the scale a station list cares
  // about and avoids per-record trigonometry.
  std::vector<size_t> nearest(double latitude, double longitude, int service, size_t wanted) const {
    struct Candidate {
      float distance_km;
      size_t index;
    };
    std::vector<Candidate> candidates;
    const double km_per_degree = 111.32;
    const double lon_scale = std::cos(latitude * 3.14159265358979323846 / 180.0);
    for (size_t index = 0; index < records_.size(); ++index) {
      const Record& record = records_[index];
      if (!record.has_position()) continue;
      if (service >= 0 && record.service != uint8_t(service)) continue;
      const double dx = (record.longitude() - longitude) * lon_scale * km_per_degree;
      const double dy = (record.latitude() - latitude) * km_per_degree;
      candidates.push_back({float(std::sqrt(dx * dx + dy * dy)), index});
    }
    const size_t take = std::min(wanted, candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + take, candidates.end(),
                      [](const Candidate& a, const Candidate& b) {
                        return a.distance_km < b.distance_km;
                      });
    std::vector<size_t> found;
    found.reserve(take);
    for (size_t n = 0; n < take; ++n) found.push_back(candidates[n].index);
    return found;
  }

  size_t find_call(const std::string& call) const {
    for (size_t index = 0; index < records_.size(); ++index)
      if (call == records_[index].call) return index;
    return records_.size();
  }

 private:
  std::vector<Record> records_;
  std::string provenance_;
};

}  // namespace orc::zero::stations
