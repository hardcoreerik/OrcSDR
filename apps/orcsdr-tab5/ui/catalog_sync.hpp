#pragma once

#include <cstdint>

#include <FS.h>

namespace orcsdr::catalog {

constexpr uint8_t kPackCount = 4;

enum class Operation : uint8_t { none, check, install, remove };

struct PackView {
  char id[20]{};
  char title[28]{};
  char version[16]{};
  char source_date[16]{};
  char status[24]{};
  uint32_t runtime_bytes = 0;
  uint32_t archive_bytes = 0;
  bool installed = false;
  bool update_available = false;
};

struct State {
  bool ready = false;
  bool busy = false;
  Operation operation = Operation::none;
  uint8_t progress_percent = 0;
  char message[80]{};
  char catalog_date[16]{};
  PackView packs[kPackCount]{};
};

// The caller owns FS lifetime and must call only from normal application code,
// never USB/IQ/audio callbacks.
void begin(fs::FS* filesystem, uint64_t free_bytes = 0);
void poll(bool wifi_connected);
bool request_check(bool wifi_connected);
bool request_install(uint8_t pack_index, bool wifi_connected);
bool request_remove(uint8_t pack_index);
State state();

}  // namespace orcsdr::catalog
