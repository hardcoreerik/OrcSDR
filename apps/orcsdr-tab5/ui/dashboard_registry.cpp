#include "dashboard_registry.hpp"

#include <algorithm>
#include <array>

namespace orcsdr::dashboards {
namespace {

constexpr Descriptor kEntries[] = {
    {Id::fm, "FM RADIO", "Broadcast FM, stereo and RDS", Category::audio, true},
    {Id::p25, "P25 RADIO", "Trunking monitor and voice follow", Category::digital, true},
    {Id::adsb, "ADS-B", "1090 MHz aircraft tracking", Category::aviation, true},
    {Id::shortwave, "SHORTWAVE", "General HF receiver workspace", Category::audio, true},
    {Id::weather, "WEATHER", "NOAA weather radio", Category::audio, true},
    {Id::cb, "CB RADIO", "40-channel AM/SSB receiver", Category::audio, true},
    {Id::lora, "LORA", "LoRa and Meshtastic receive tools", Category::digital, true},
    {Id::airband, "AIRBAND", "VHF aviation voice", Category::aviation, true},
    {Id::marine, "MARINE", "VHF marine receiver", Category::audio, true},
    {Id::satellite, "SATELLITE", "Satellite receive workspace", Category::digital, true},
    {Id::settings, "SETTINGS", "Global device settings", Category::system, true},
};

std::array<Id, kRecentCapacity> g_recent{};
size_t g_recent_count = 0;

bool valid_recent(Id id) {
  return id != Id::home && id != Id::count && find(id) != nullptr;
}

}  // namespace

const Descriptor* find(Id id) {
  for (const auto& entry : kEntries)
    if (entry.id == id) return &entry;
  return nullptr;
}

const Descriptor* descriptor(size_t index) {
  return index < std::size(kEntries) ? &kEntries[index] : nullptr;
}

size_t count() { return std::size(kEntries); }

void load_recent(const uint8_t* ids, size_t count_value) {
  g_recent_count = 0;
  if (ids != nullptr) {
    for (size_t i = 0; i < count_value && i < kRecentCapacity; ++i) {
      const Id id = static_cast<Id>(ids[i]);
      if (!valid_recent(id)) continue;
      bool duplicate = false;
      for (size_t j = 0; j < g_recent_count; ++j) duplicate |= g_recent[j] == id;
      if (!duplicate) g_recent[g_recent_count++] = id;
    }
  }
  if (g_recent_count == 0) {
    constexpr Id defaults[] = {Id::fm, Id::p25, Id::adsb, Id::shortwave,
                               Id::lora, Id::cb, Id::weather};
    std::copy(std::begin(defaults), std::end(defaults), g_recent.begin());
    g_recent_count = std::size(defaults);
  }
}

bool record_open(Id id) {
  if (!valid_recent(id)) return false;
  size_t old = g_recent_count;
  for (size_t i = 0; i < g_recent_count; ++i) {
    if (g_recent[i] != id) continue;
    old = i;
    break;
  }
  if (old == 0) return false;
  if (old == g_recent_count && g_recent_count < kRecentCapacity) ++g_recent_count;
  const size_t last = std::min(old, g_recent_count - 1);
  for (size_t i = last; i > 0; --i) g_recent[i] = g_recent[i - 1];
  g_recent[0] = id;
  return true;
}

size_t recent_count() { return g_recent_count; }

Id recent(size_t index) { return index < g_recent_count ? g_recent[index] : Id::count; }

size_t copy_recent(uint8_t* ids, size_t capacity) {
  const size_t copied = std::min(capacity, g_recent_count);
  for (size_t i = 0; i < copied; ++i) ids[i] = static_cast<uint8_t>(g_recent[i]);
  return copied;
}

bool self_check() {
  std::array<Id, kRecentCapacity> saved = g_recent;
  const size_t saved_count = g_recent_count;
  const uint8_t seed[] = {static_cast<uint8_t>(Id::fm), static_cast<uint8_t>(Id::p25),
                          static_cast<uint8_t>(Id::fm), 255};
  load_recent(seed, std::size(seed));
  const bool loaded = g_recent_count == 2 && g_recent[0] == Id::fm &&
                      g_recent[1] == Id::p25;
  const bool moved = record_open(Id::p25) && g_recent[0] == Id::p25 &&
                     g_recent[1] == Id::fm && !record_open(Id::p25);
  g_recent = saved;
  g_recent_count = saved_count;
  return loaded && moved && std::size(kEntries) == 11;
}

}  // namespace orcsdr::dashboards
