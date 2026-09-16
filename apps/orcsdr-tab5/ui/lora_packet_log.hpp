#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::storage { class FileSystem; }

namespace orcsdr::lora_log {

struct Record {
  char text[112]{};
  uint32_t sender = 0;
  uint32_t destination = 0;
  uint32_t packet_id = 0;
  uint32_t received_ms = 0;
  uint32_t received_utc = 0;
  int32_t latitude_e7 = INT32_MAX;
  int32_t longitude_e7 = INT32_MAX;
  int16_t snr_tenths = INT16_MAX;
  int16_t signal_tenths = INT16_MAX;
  uint16_t port = 0;
  uint32_t frequency_hz = 0;
  bool encrypted = false;
  char short_name[8]{};
  char long_name[32]{};
};

size_t format_csv(char* output, size_t output_size, const Record& record);
bool export_snapshot(storage::FileSystem& filesystem, const Record* records, size_t count,
                     uint16_t* sequence, char* path, size_t path_size);
bool self_check();

}  // namespace orcsdr::lora_log
