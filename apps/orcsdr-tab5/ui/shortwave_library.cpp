#include "shortwave_library.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace orcsdr::shortwave {
namespace {

class Writer {
 public:
  Writer(char* output, size_t capacity) : output_(output), capacity_(capacity) {
    if (capacity_) output_[0] = '\0';
  }

  bool append(const char* value) {
    const size_t length = std::strlen(value);
    if (!ok_ || used_ + length >= capacity_) return ok_ = false;
    std::memcpy(output_ + used_, value, length + 1);
    used_ += length;
    return true;
  }

  bool csv(const char* value, bool first = false) {
    if (!first && !append(",")) return false;
    if (!append("\"")) return false;
    for (const char* p = value; *p; ++p) {
      char character[2] = {*p, '\0'};
      if (*p == '\"' && !append("\"")) return false;
      if (!append(character)) return false;
    }
    return append("\"");
  }

  bool tag(const char* name, const char* value) {
    char prefix[48];
    std::snprintf(prefix, sizeof(prefix), "<%s:%u>", name,
                  static_cast<unsigned>(std::strlen(value)));
    return append(prefix) && append(value);
  }

  bool ok() const { return ok_; }

 private:
  char* output_;
  size_t capacity_;
  size_t used_ = 0;
  bool ok_ = true;
};

template <typename Number>
bool csv_number(Writer& writer, Number value, const char* format, bool first = false) {
  char text[40];
  std::snprintf(text, sizeof(text), format, value);
  return writer.csv(text, first);
}

bool parse_csv(const char* input, char fields[][256], size_t expected) {
  if (!input) return false;
  const char* cursor = input;
  for (size_t field = 0; field < expected; ++field) {
    if (*cursor != '\"') return false;
    ++cursor;
    size_t used = 0;
    bool closed = false;
    while (*cursor) {
      if (*cursor == '\"') {
        if (cursor[1] == '\"') {
          if (used + 1 >= 256) return false;
          fields[field][used++] = '\"';
          cursor += 2;
          continue;
        }
        ++cursor;
        closed = true;
        break;
      }
      if (used + 1 >= 256) return false;
      fields[field][used++] = *cursor++;
    }
    if (!closed) return false;
    fields[field][used] = '\0';
    if (field + 1 < expected) {
      if (*cursor != ',') return false;
      ++cursor;
    } else if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
      return false;
    }
  }
  return true;
}

template <typename Number>
bool parse_integer(const char* text, Number* value) {
  char* end = nullptr;
  const long long parsed = std::strtoll(text, &end, 10);
  if (!*text || *end) return false;
  *value = static_cast<Number>(parsed);
  return static_cast<long long>(*value) == parsed;
}

template <size_t Size>
bool copy_field(char (&destination)[Size], const char* source) {
  if (std::strlen(source) >= Size) return false;
  std::strcpy(destination, source);
  return true;
}

bool parse_signal(const char* text, float* value) {
  char* end = nullptr;
  const float parsed = std::strtof(text, &end);
  if (!*text || *end || !std::isfinite(parsed)) return false;
  *value = parsed;
  return true;
}

}  // namespace

bool encode_memory_csv(const Memory& memory, char* output, size_t capacity) {
  if (!output || !capacity || !valid(memory)) return false;
  Writer writer(output, capacity);
  return csv_number(writer, memory.frequency_hz, "%u", true) &&
         csv_number(writer, memory.bandwidth_hz, "%u") &&
         csv_number(writer, static_cast<unsigned long long>(memory.saved_utc), "%llu") &&
         csv_number(writer, memory.favorite ? 1 : 0, "%d") &&
         writer.csv(memory.mode) && writer.csv(memory.station) &&
         writer.csv(memory.callsign) && writer.csv(memory.country) &&
         writer.csv(memory.language) && writer.csv(memory.notes) && writer.ok();
}

bool decode_memory_csv(const char* input, Memory* memory) {
  if (!memory) return false;
  char fields[10][256]{};
  Memory parsed{};
  uint32_t favorite = 0;
  if (!parse_csv(input, fields, 10) ||
      !parse_integer(fields[0], &parsed.frequency_hz) ||
      !parse_integer(fields[1], &parsed.bandwidth_hz) ||
      !parse_integer(fields[2], &parsed.saved_utc) ||
      !parse_integer(fields[3], &favorite) || favorite > 1 ||
      !copy_field(parsed.mode, fields[4]) || !copy_field(parsed.station, fields[5]) ||
      !copy_field(parsed.callsign, fields[6]) || !copy_field(parsed.country, fields[7]) ||
      !copy_field(parsed.language, fields[8]) || !copy_field(parsed.notes, fields[9]))
    return false;
  parsed.favorite = favorite != 0;
  if (!valid(parsed)) return false;
  *memory = parsed;
  return true;
}

bool encode_log_csv(const LogEntry& entry, char* output, size_t capacity) {
  if (!output || !capacity || !valid(entry)) return false;
  Writer writer(output, capacity);
  return csv_number(writer, static_cast<unsigned long long>(entry.timestamp_utc), "%llu", true) &&
         csv_number(writer, entry.local_offset_minutes, "%d") &&
         csv_number(writer, entry.frequency_hz, "%u") &&
         csv_number(writer, entry.bandwidth_hz, "%u") &&
         csv_number(writer, entry.signal_dbfs, "%.3f") &&
         writer.csv(entry.mode) && writer.csv(entry.station) &&
         writer.csv(entry.program) && writer.csv(entry.callsign) &&
         writer.csv(entry.country) && writer.csv(entry.language) &&
         writer.csv(entry.device) && writer.csv(entry.antenna) &&
         writer.csv(entry.notes) && writer.csv(entry.recording_path) && writer.ok();
}

bool decode_log_csv(const char* input, LogEntry* entry) {
  if (!entry) return false;
  char fields[15][256]{};
  LogEntry parsed{};
  if (!parse_csv(input, fields, 15) ||
      !parse_integer(fields[0], &parsed.timestamp_utc) ||
      !parse_integer(fields[1], &parsed.local_offset_minutes) ||
      !parse_integer(fields[2], &parsed.frequency_hz) ||
      !parse_integer(fields[3], &parsed.bandwidth_hz) ||
      !parse_signal(fields[4], &parsed.signal_dbfs) ||
      !copy_field(parsed.mode, fields[5]) || !copy_field(parsed.station, fields[6]) ||
      !copy_field(parsed.program, fields[7]) || !copy_field(parsed.callsign, fields[8]) ||
      !copy_field(parsed.country, fields[9]) || !copy_field(parsed.language, fields[10]) ||
      !copy_field(parsed.device, fields[11]) || !copy_field(parsed.antenna, fields[12]) ||
      !copy_field(parsed.notes, fields[13]) || !copy_field(parsed.recording_path, fields[14]) ||
      !valid(parsed))
    return false;
  *entry = parsed;
  return true;
}

bool encode_adif(const LogEntry& entry, char* output, size_t capacity) {
  if (!output || !capacity || !valid(entry)) return false;
  const std::time_t timestamp = static_cast<std::time_t>(entry.timestamp_utc);
  const std::tm* utc = std::gmtime(&timestamp);
  if (!utc) return false;
  char date[9], time[7], frequency[24], comment[640];
  if (std::strftime(date, sizeof(date), "%Y%m%d", utc) != 8 ||
      std::strftime(time, sizeof(time), "%H%M%S", utc) != 6)
    return false;
  std::snprintf(frequency, sizeof(frequency), "%.4f",
                static_cast<double>(entry.frequency_hz) / 1000000.0);
  std::snprintf(comment, sizeof(comment),
                "OrcSDR listener log; station=%s; program=%s; language=%s; antenna=%s; device=%s; bandwidth=%u Hz; signal=%.1f dBFS; notes=%s",
                entry.station, entry.program, entry.language, entry.antenna, entry.device,
                static_cast<unsigned>(entry.bandwidth_hz),
                static_cast<double>(entry.signal_dbfs), entry.notes);
  Writer writer(output, capacity);
  if (!writer.append("Generated by OrcSDR<EOH>")) return false;
  if (!writer.tag("QSO_DATE", date) || !writer.tag("TIME_ON", time) ||
      !writer.tag("FREQ", frequency) || !writer.tag("MODE", entry.mode) ||
      !writer.tag("SWL", "Y"))
    return false;
  if (entry.callsign[0] && !writer.tag("CALL", entry.callsign)) return false;
  return writer.tag("COMMENT", comment) && writer.append("<EOR>\n") && writer.ok();
}

bool library_self_check() {
  Memory memory{};
  memory.frequency_hz = 9800000;
  memory.bandwidth_hz = 6000;
  std::strcpy(memory.mode, "AM");
  std::strcpy(memory.notes, "Signal, then voice");
  char text[1024];
  Memory decoded{};
  return encode_memory_csv(memory, text, sizeof(text)) &&
         decode_memory_csv(text, &decoded) &&
         std::strcmp(decoded.notes, memory.notes) == 0;
}

}  // namespace orcsdr::shortwave
