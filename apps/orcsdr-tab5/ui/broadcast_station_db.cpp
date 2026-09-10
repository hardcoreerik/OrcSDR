#include "broadcast_station_db.hpp"

#include "orcsdr_storage.hpp"

#include <cJSON.h>

#include <cstring>

namespace orcsdr::broadcast {
namespace {

constexpr uint8_t kMagic[] = {'O', 'R', 'C', 'B', 'R', 'D', '1', '\n'};
constexpr uint32_t kHeaderBytes = 32;
constexpr uint32_t kIndexBytes = 8;
constexpr uint32_t kMaxRecords = 250000;
constexpr uint32_t kMaxRecordBytes = 2048;

uint16_t little16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8);
}

uint32_t little32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
}

bool read_exact(File& file, uint8_t* output, size_t bytes) {
  return output != nullptr && file.read(output, bytes) == bytes;
}

const cJSON* field(const cJSON* object, const char* name) {
  return object ? cJSON_GetObjectItemCaseSensitive(object, name) : nullptr;
}

void copy_text(const cJSON* value, char* output, size_t output_size) {
  if (!output || output_size == 0) return;
  output[0] = '\0';
  if (cJSON_IsString(value) && value->valuestring) strlcpy(output, value->valuestring, output_size);
}

uint32_t number_u32(const cJSON* value) {
  return cJSON_IsNumber(value) && value->valuedouble >= 0 && value->valuedouble <= UINT32_MAX
      ? static_cast<uint32_t>(value->valuedouble) : 0;
}

int32_t number_i32(const cJSON* value) {
  return cJSON_IsNumber(value) && value->valuedouble >= INT32_MIN && value->valuedouble <= INT32_MAX
      ? static_cast<int32_t>(value->valuedouble) : 0;
}

Service parse_service(const cJSON* value) {
  if (!cJSON_IsString(value) || !value->valuestring) return Service::unknown;
  if (strcmp(value->valuestring, "am") == 0) return Service::am;
  if (strcmp(value->valuestring, "fm") == 0) return Service::fm;
  if (strcmp(value->valuestring, "shortwave") == 0) return Service::shortwave;
  return Service::unknown;
}

bool parse_card(const char* text, size_t size, StationCard* output) {
  if (!text || !size || !output) return false;
  cJSON* root = cJSON_ParseWithLength(text, size);
  if (!root) return false;
  StationCard card{};
  card.frequency_hz = number_u32(field(root, "frequency_hz"));
  card.power_w = number_u32(field(root, "power_w"));
  card.latitude_e7 = number_i32(field(root, "latitude_e7"));
  card.longitude_e7 = number_i32(field(root, "longitude_e7"));
  card.service = parse_service(field(root, "service"));
  copy_text(field(root, "id"), card.id, sizeof(card.id));
  copy_text(field(root, "source"), card.source, sizeof(card.source));
  copy_text(field(root, "source_id"), card.source_id, sizeof(card.source_id));
  copy_text(field(root, "callsign"), card.callsign, sizeof(card.callsign));
  copy_text(field(root, "name"), card.name, sizeof(card.name));
  copy_text(field(root, "city"), card.city, sizeof(card.city));
  copy_text(field(root, "region"), card.region, sizeof(card.region));
  copy_text(field(root, "country"), card.country, sizeof(card.country));
  copy_text(field(root, "status"), card.status, sizeof(card.status));
  copy_text(field(root, "mode"), card.mode, sizeof(card.mode));
  copy_text(field(root, "language"), card.language, sizeof(card.language));
  copy_text(field(root, "utc_start"), card.utc_start, sizeof(card.utc_start));
  copy_text(field(root, "utc_stop"), card.utc_stop, sizeof(card.utc_stop));
  copy_text(field(root, "days"), card.days, sizeof(card.days));
  copy_text(field(root, "season"), card.season, sizeof(card.season));
  copy_text(field(root, "transmitter"), card.transmitter, sizeof(card.transmitter));
  copy_text(field(root, "target"), card.target, sizeof(card.target));
  copy_text(field(root, "rds_ps"), card.rds_ps, sizeof(card.rds_ps));
  copy_text(field(root, "rds_pi"), card.rds_pi, sizeof(card.rds_pi));
  cJSON_Delete(root);
  if (!card.frequency_hz || card.service == Service::unknown || !card.id[0] || !card.source[0]) return false;
  *output = card;
  return true;
}

bool read_index(File& file, uint32_t index_offset, uint32_t index, uint32_t* frequency,
                uint32_t* record_offset) {
  uint8_t bytes[kIndexBytes]{};
  const uint64_t position = static_cast<uint64_t>(index_offset) + static_cast<uint64_t>(index) * kIndexBytes;
  return position <= UINT32_MAX && file.seek(static_cast<size_t>(position)) &&
         read_exact(file, bytes, sizeof(bytes)) &&
         ((*frequency = little32(bytes)), (*record_offset = little32(bytes + 4)), true);
}

}  // namespace

const char* service_label(Service service) {
  switch (service) {
    case Service::am: return "AM";
    case Service::fm: return "FM";
    case Service::shortwave: return "SHORTWAVE";
    default: return "UNKNOWN";
  }
}

void Database::begin(orcsdr::storage::FileSystem* filesystem, const char* path) {
  filesystem_ = filesystem;
  path_ = path && path[0] ? path : kDefaultPath;
  (void)refresh();
}

bool Database::refresh() {
  available_ = false;
  record_count_ = index_offset_ = records_offset_ = 0;
  if (!filesystem_ || !path_ || !filesystem_->exists(path_)) return false;
  File file = filesystem_->open(path_, FILE_READ);
  uint8_t header[kHeaderBytes]{};
  if (!file || !read_exact(file, header, sizeof(header)) || memcmp(header, kMagic, sizeof(kMagic)) != 0 ||
      little16(header + 8) != 1 || little16(header + 10) != kHeaderBytes) return false;
  const uint32_t records = little32(header + 12);
  const uint32_t index = little32(header + 16);
  const uint32_t data = little32(header + 20);
  const uint32_t metadata = little32(header + 24);
  const uint32_t metadata_bytes = little32(header + 28);
  const uint64_t expected_index = static_cast<uint64_t>(metadata) + metadata_bytes;
  const uint64_t expected_data = static_cast<uint64_t>(index) + static_cast<uint64_t>(records) * kIndexBytes;
  if (!records || records > kMaxRecords || metadata != kHeaderBytes || index != expected_index ||
      data != expected_data || data > file.size()) return false;
  record_count_ = records;
  index_offset_ = index;
  records_offset_ = data;
  available_ = true;
  return true;
}

size_t Database::lookup_frequency(uint32_t frequency_hz, StationCard* output, size_t capacity) const {
  if (!available_ || !filesystem_ || !output || capacity == 0) return 0;
  File file = filesystem_->open(path_, FILE_READ);
  if (!file) return 0;
  uint32_t low = 0, high = record_count_;
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2;
    uint32_t indexed_frequency = 0, ignored = 0;
    if (!read_index(file, index_offset_, middle, &indexed_frequency, &ignored)) return 0;
    if (indexed_frequency < frequency_hz) low = middle + 1;
    else high = middle;
  }
  size_t found = 0;
  for (uint32_t index = low; index < record_count_ && found < capacity; ++index) {
    uint32_t indexed_frequency = 0, record_offset = 0;
    if (!read_index(file, index_offset_, index, &indexed_frequency, &record_offset) || indexed_frequency != frequency_hz)
      break;
    const uint64_t position = static_cast<uint64_t>(records_offset_) + record_offset;
    uint8_t length_bytes[4]{};
    if (position > UINT32_MAX || !file.seek(static_cast<size_t>(position)) ||
        !read_exact(file, length_bytes, sizeof(length_bytes))) break;
    const uint32_t length = little32(length_bytes);
    if (!length || length > kMaxRecordBytes) continue;
    char record[kMaxRecordBytes + 1]{};
    if (!read_exact(file, reinterpret_cast<uint8_t*>(record), length)) break;
    if (parse_card(record, length, &output[found]) && output[found].frequency_hz == frequency_hz) ++found;
  }
  return found;
}

}  // namespace orcsdr::broadcast
