#include "receiver_location.hpp"

#include <cstring>

namespace orcsdr::receiver_location {
namespace {

Snapshot g_location{};

void copy_text(char* destination, size_t size, const char* value) {
  if (size == 0) return;
  if (value == nullptr) {
    destination[0] = '\0';
    return;
  }
  std::strncpy(destination, value, size - 1);
  destination[size - 1] = '\0';
}

bool text_equal(const char* a, const char* b) {
  return std::strcmp(a ? a : "", b ? b : "") == 0;
}

}  // namespace

bool valid(bool configured, int32_t latitude_e7, int32_t longitude_e7) {
  if (!configured) return true;
  return latitude_e7 >= -900000000 && latitude_e7 <= 900000000 &&
         longitude_e7 >= -1800000000 && longitude_e7 <= 1800000000;
}

void set(bool configured, int32_t latitude_e7, int32_t longitude_e7,
         const char* label, const char* map_pack) {
  if (!valid(configured, latitude_e7, longitude_e7)) return;
  if (!configured) {
    latitude_e7 = 0;
    longitude_e7 = 0;
  }
  const bool changed = g_location.configured != configured ||
                       g_location.latitude_e7 != latitude_e7 ||
                       g_location.longitude_e7 != longitude_e7 ||
                       !text_equal(g_location.label, label) ||
                       !text_equal(g_location.map_pack, map_pack);
  if (!changed) return;
  g_location.configured = configured;
  g_location.latitude_e7 = latitude_e7;
  g_location.longitude_e7 = longitude_e7;
  copy_text(g_location.label, sizeof(g_location.label), label);
  copy_text(g_location.map_pack, sizeof(g_location.map_pack), map_pack);
  ++g_location.revision;
  if (g_location.revision == 0) g_location.revision = 1;
}

Snapshot snapshot() { return g_location; }

bool self_check() {
  const Snapshot saved = g_location;
  g_location = {};
  set(true, 440462000, -1230220000, "Springfield, Oregon", "Willamette Valley");
  const Snapshot first = snapshot();
  const uint32_t revision = first.revision;
  set(true, 440462000, -1230220000, "Springfield, Oregon", "Willamette Valley");
  const bool stable = snapshot().revision == revision;
  set(false, 123, 456, nullptr, nullptr);
  const Snapshot cleared = snapshot();
  const bool ok = first.configured && first.latitude_e7 == 440462000 &&
                  first.longitude_e7 == -1230220000 &&
                  std::strcmp(first.label, "Springfield, Oregon") == 0 &&
                  stable && !cleared.configured && cleared.latitude_e7 == 0 &&
                  cleared.longitude_e7 == 0 &&
                  valid(true, 900000000, 1800000000) &&
                  !valid(true, 900000001, 0);
  g_location = saved;
  return ok;
}

}  // namespace orcsdr::receiver_location
