#include "map_packs.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#if !defined(ORCSDR_MAP_PACKS_HOST_TEST)
#include "orcmap/esp_idf/pack_filesystem.hpp"
#include "orcmap/pack.hpp"
#include "orcmap/pack_discovery.hpp"
#endif

namespace orcsdr::map_packs {
namespace {

#if !defined(ORCSDR_MAP_PACKS_HOST_TEST)
Summary g_summary;
char g_best_path[128]{};
uint8_t g_best_zoom = 0;
PackInfo g_infos[kInfoMax]{};
size_t g_info_count = 0;
uint32_t g_generation = 0;

void copy(char* out, size_t size, const std::string& value) {
  std::snprintf(out, size, "%s", value.c_str());
}
#endif

}  // namespace

bool covers_world(double min_lon, double min_lat, double max_lon, double max_lat) {
  // The Web Mercator world is +-85.05 degrees; a world overview is built to that, so allow a little slack.
  return min_lon <= -179.0 && max_lon >= 179.0 && min_lat <= -84.0 && max_lat >= 84.0;
}

void format_size(char* out, size_t size, uint32_t size_kib) {
  if (out == nullptr || size == 0) return;
  if (size_kib < 1024) std::snprintf(out, size, "%u KB", static_cast<unsigned>(size_kib));
  else if (size_kib < 1024u * 1024u) std::snprintf(out, size, "%.1f MB", size_kib / 1024.0);
  else std::snprintf(out, size, "%.1f GB", size_kib / (1024.0 * 1024.0));
}

#if !defined(ORCSDR_MAP_PACKS_HOST_TEST)
void scan() {
  Summary next;
  next.scanned = true;
  g_best_path[0] = '\0';
  g_best_zoom = 0;
  g_info_count = 0;
  ++g_generation;

  orcmap::esp_idf::PackFileSystem filesystem;
  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  (void)orcmap::DiscoverPacks(filesystem, kDirectory, &catalog, &report);
  next.directory_listed = report.directory_listed;

  for (const orcmap::PackManifest& pack : catalog.Packs()) {
    ++next.pack_count;
    const bool world = covers_world(pack.bounds.min_lon_deg, pack.bounds.min_lat_deg, pack.bounds.max_lon_deg,
                                    pack.bounds.max_lat_deg);
    if (world && pack.max_zoom >= g_best_zoom && !pack.archive_path.empty()) {
      g_best_zoom = pack.max_zoom;
      std::snprintf(g_best_path, sizeof(g_best_path), "%s", pack.archive_path.c_str());
    }
    if (g_info_count < kInfoMax && !pack.archive_path.empty()) {
      PackInfo& info = g_infos[g_info_count++];
      info = PackInfo{};
      std::snprintf(info.path, sizeof(info.path), "%s", pack.archive_path.c_str());
      info.min_zoom = pack.min_zoom;
      info.max_zoom = pack.max_zoom;
      info.min_lat = static_cast<float>(pack.bounds.min_lat_deg);
      info.min_lon = static_cast<float>(pack.bounds.min_lon_deg);
      info.max_lat = static_cast<float>(pack.bounds.max_lat_deg);
      info.max_lon = static_cast<float>(pack.bounds.max_lon_deg);
      info.world = world;
      if (!pack.attribution.empty())
        std::snprintf(info.credit, sizeof(info.credit), "%s", pack.attribution[0].text.c_str());
    }
    if (next.shown >= kViewMax) continue;
    Entry& entry = next.entries[next.shown++];
    copy(entry.name, sizeof(entry.name), pack.display_name.empty() ? pack.pack_id : pack.display_name);
    copy(entry.region, sizeof(entry.region), pack.region_name);
    entry.min_zoom = pack.min_zoom;
    entry.max_zoom = pack.max_zoom;
    entry.size_kib = static_cast<uint32_t>(pack.size_bytes / 1024u);
    entry.valid = true;
    entry.world = world;
    std::snprintf(entry.note, sizeof(entry.note), "READY");
  }
  for (const orcmap::RejectedPack& rejected : report.rejected) {
    ++next.rejected_count;
    if (next.shown >= kViewMax) continue;
    Entry& entry = next.entries[next.shown++];
    copy(entry.name, sizeof(entry.name), rejected.manifest_name);
    std::snprintf(entry.note, sizeof(entry.note), "%s", orcmap::PackRejectionName(rejected.rejection));
  }
  g_summary = next;
  std::printf("RTL_MAP_PACKS dir=%s listed=%d valid=%u rejected=%u best_world_zoom=%u\n", kDirectory,
              next.directory_listed ? 1 : 0, static_cast<unsigned>(next.pack_count),
              static_cast<unsigned>(next.rejected_count), static_cast<unsigned>(g_best_zoom));
}

void scan_if_needed() {
  if (!g_summary.scanned) scan();
}

Summary summary() { return g_summary; }

size_t valid_packs(PackInfo* out, size_t capacity) {
  if (out == nullptr) return 0;
  const size_t count = g_info_count < capacity ? g_info_count : capacity;
  for (size_t i = 0; i < count; ++i) out[i] = g_infos[i];
  return count;
}

uint32_t generation() { return g_generation; }

bool best_world_archive(char* path, size_t size, uint8_t* max_zoom) {
  if (path == nullptr || size == 0 || g_best_path[0] == '\0') return false;
  std::snprintf(path, size, "%s", g_best_path);
  if (max_zoom != nullptr) *max_zoom = g_best_zoom;
  return true;
}
#endif

bool self_check() {
  char text[16];
  format_size(text, sizeof(text), 812);
  const bool kb = std::strcmp(text, "812 KB") == 0;
  format_size(text, sizeof(text), 9506);
  const bool mb = std::strcmp(text, "9.3 MB") == 0;
  format_size(text, sizeof(text), 1258291);
  const bool gb = std::strcmp(text, "1.2 GB") == 0;
  return kb && mb && gb && covers_world(-180.0, -85.05, 180.0, 85.05) && !covers_world(-124.7, 41.9, -116.4, 46.3) &&
         !covers_world(-180.0, 0.0, 180.0, 85.0) && !covers_world(-90.0, -85.0, 90.0, 85.0);
}

}  // namespace orcsdr::map_packs
