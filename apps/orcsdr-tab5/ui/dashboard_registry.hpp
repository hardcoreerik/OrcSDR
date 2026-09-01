#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::dashboards {

enum class Id : uint8_t {
  home,
  fm,
  p25,
  adsb,
  shortwave,
  weather,
  cb,
  lora,
  airband,
  marine,
  satellite,
  utilities,
  settings,
  rf_lab,
  wifi_analysis,
  count,
};

enum class Category : uint8_t { audio, digital, aviation, utility, system };

struct Descriptor {
  Id id;
  const char* title;
  const char* subtitle;
  Category category;
  bool available;
};

constexpr size_t kRecentCapacity = 12;

const Descriptor* find(Id id);
const Descriptor* descriptor(size_t index);
size_t count();
void load_recent(const uint8_t* ids, size_t count);
bool record_open(Id id);
size_t recent_count();
Id recent(size_t index);
size_t copy_recent(uint8_t* ids, size_t capacity);
bool self_check();

}  // namespace orcsdr::dashboards
