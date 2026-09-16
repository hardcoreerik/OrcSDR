#include "lora_packet_log.hpp"
#include "orcsdr_storage.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::lora_log {

size_t format_csv(char* output, size_t output_size, const Record& record) {
  if (output == nullptr || output_size < 4) return 0;
  char destination[16];
  if (record.destination == UINT32_MAX)
    strlcpy(destination, "broadcast", sizeof(destination));
  else
    snprintf(destination, sizeof(destination), "!%08lx",
             static_cast<unsigned long>(record.destination));
  const int used = snprintf(
      output, output_size, "%lu,%u,%s,%lu,%lu,!%08lx,%s,%08lx,%u,%d,%d,%ld,%ld,\"",
      static_cast<unsigned long>(record.received_ms),
      record.received_utc ? 1u : 0u,
      record.received_utc ? "rtc" : "none",
      static_cast<unsigned long>(record.received_utc),
      static_cast<unsigned long>(record.frequency_hz),
      static_cast<unsigned long>(record.sender), destination,
      static_cast<unsigned long>(record.packet_id), static_cast<unsigned>(record.port),
      static_cast<int>(record.snr_tenths), static_cast<int>(record.signal_tenths),
      static_cast<long>(record.latitude_e7), static_cast<long>(record.longitude_e7));
  if (used < 0 || static_cast<size_t>(used) + 3 > output_size) return 0;
  size_t position = static_cast<size_t>(used);
  for (const char* text = record.text; *text && position + 4 < output_size; ++text) {
    if (*text == '"') output[position++] = '"';
    output[position++] = *text;
  }
  output[position++] = '"';
  output[position++] = '\n';
  output[position] = '\0';
  return position;
}

bool export_snapshot(storage::FileSystem& filesystem, const Record* records, size_t count,
                     uint16_t* sequence, char* path, size_t path_size) {
  if (records == nullptr || count == 0 || sequence == nullptr || path == nullptr ||
      path_size < 24) return false;
  if (!filesystem.exists("/orcsdr") && !filesystem.mkdir("/orcsdr")) return false;
  do {
    snprintf(path, path_size, "/orcsdr/lora_%03u.csv", ++*sequence);
  } while (filesystem.exists(path));
  storage::File file = filesystem.open(path, FILE_WRITE, true);
  if (!file) return false;
  file.print("uptime_ms,wallclock_valid,wallclock_source,received_utc,frequency_hz,from,to,packet_id,port,snr_tenths,"
             "signal_tenths,latitude_e7,longitude_e7,text\n");
  char line[512];
  for (size_t i = count; i > 0; --i) {
    const size_t bytes = format_csv(line, sizeof(line), records[i - 1]);
    if (bytes == 0 || file.write(reinterpret_cast<const uint8_t*>(line), bytes) != bytes) {
      file.close();
      return false;
    }
  }
  file.flush();
  file.close();
  return true;
}

bool self_check() {
  Record record{};
  record.sender = 0x435baa2c;
  record.destination = UINT32_MAX;
  record.packet_id = 7;
  record.received_ms = 42;
  record.received_utc = 1789000000;
  record.frequency_hz = 906875000;
  strlcpy(record.text, "test \"one\"", sizeof(record.text));
  char line[256];
  const size_t bytes = format_csv(line, sizeof(line), record);
  const bool content_ok = bytes > 0 &&
                          strstr(line, "42,1,rtc,1789000000,906875000") != nullptr &&
                          strstr(line, "!435baa2c,broadcast,00000007") != nullptr &&
                          strstr(line, "\"test \"\"one\"\"\"") != nullptr;
  record.text[0] = '\0';
  const size_t empty_bytes = format_csv(line, sizeof(line), record);
  return content_ok && empty_bytes > 0 && format_csv(line, empty_bytes, record) == 0;
}

}  // namespace orcsdr::lora_log
