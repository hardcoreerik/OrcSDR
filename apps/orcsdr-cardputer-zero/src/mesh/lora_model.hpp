#pragma once
// One model for every LoRa-band implementation the dashboard can hear.
//
// The lesson from PotatoMesh, meshmonitor and the MeshCore tools is that nobody
// supports several protocols by writing several dashboards. They write one data
// model and one adapter per protocol. This is that model: the screens (TRAFFIC,
// NODES, MAP, MESSAGES, STATS) read it without knowing which radio or protocol
// produced a record, and an adapter's whole job is to turn its own frame format
// into Node and Traffic records.
//
// Nothing here decodes anything. Meshtastic decoding lives in
// meshtastic_packet.hpp on top of the shared LoRa PHY decoder; MeshCore and the
// rest get their own adapters that fill in these same structs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace orc::zero::mesh {

enum class Protocol : uint8_t { unknown, meshtastic, meshcore, reticulum, aprs, count };

inline const char* protocol_name(Protocol protocol) {
  switch (protocol) {
    case Protocol::meshtastic: return "MESHTASTIC";
    case Protocol::meshcore: return "MESHCORE";
    case Protocol::reticulum: return "RETICULUM";
    case Protocol::aprs: return "LORA APRS";
    default: return "UNKNOWN";
  }
}

// A peer as heard. `id` is the protocol's own identifier (a Meshtastic node
// number here); `name` is whatever the protocol gives us to display, falling
// back to a hex id when it gives nothing.
struct Node {
  uint32_t id = 0;
  Protocol protocol = Protocol::unknown;
  char name[16]{};
  uint32_t packets = 0;
  uint32_t port_last = 0;
  int16_t snr_db = 0;
  int8_t rssi_dbm = 0;
  uint8_t hops = 0;
  bool has_signal = false;
  bool has_position = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint64_t last_heard_ms = 0;

  double latitude() const { return latitude_e7 / 1e7; }
  double longitude() const { return longitude_e7 / 1e7; }
};

// One received frame, in protocol-neutral terms. `to == kBroadcast` marks a
// broadcast for protocols that have one.
inline constexpr uint32_t kBroadcast = 0xFFFFFFFFu;

struct Traffic {
  uint32_t from = 0;
  uint32_t to = kBroadcast;
  uint32_t packet_id = 0;
  Protocol protocol = Protocol::unknown;
  uint8_t channel = 0;
  uint8_t hops = 0;
  int16_t snr_db = 0;
  int8_t rssi_dbm = 0;
  bool has_signal = false;
  uint32_t portnum = 0;
  std::string text;          // decrypted text when the protocol gave us any
  bool has_position = false;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  uint64_t heard_ms = 0;

  bool direct() const { return to != kBroadcast; }
};

// Distance in kilometres on a local tangent plane. Accurate at the scale a
// neighbourhood map cares about and cheap enough to run over every node on each
// repaint, which a per-pair haversine is not.
inline double distance_km(double lat_a, double lon_a, double lat_b, double lon_b) {
  constexpr double kDegreesToKm = 111.32;
  constexpr double kPi = 3.14159265358979323846;
  const double dx = (lon_b - lon_a) * kDegreesToKm * std::cos(lat_a * kPi / 180.0);
  const double dy = (lat_b - lat_a) * kDegreesToKm;
  return std::sqrt(dx * dx + dy * dy);
}

// Bounded node table. A crowded mesh can hear hundreds of nodes; the screen can
// show a handful, so the table keeps the recently-heard and evicts the quietest
// rather than refusing new arrivals.
class Model {
 public:
  static constexpr size_t kCapacity = 64;

  void clear() { nodes_.clear(); }

  const std::vector<Node>& nodes() const { return nodes_; }
  size_t size() const { return nodes_.size(); }

  // Records a frame: updates the sender, or creates it. Returns the node index.
  size_t observe(const Traffic& traffic) {
    size_t index = 0;
    while (index < nodes_.size() && nodes_[index].id != traffic.from) ++index;
    if (index == nodes_.size()) {
      if (nodes_.size() == kCapacity) index = weakest_index();
      else nodes_.push_back(Node{});
    }
    Node& node = nodes_[index];
    if (node.packets == 0) {  // first sighting fills the identity
      node.id = traffic.from;
      node.protocol = traffic.protocol;
      format_fallback_name(node);
    }
    ++node.packets;
    node.port_last = traffic.portnum;
    node.last_heard_ms = traffic.heard_ms;
    if (traffic.has_signal) {
      node.snr_db = traffic.snr_db;
      node.rssi_dbm = traffic.rssi_dbm;
      node.has_signal = true;
    }
    node.hops = traffic.hops;
    if (traffic.has_position) {
      node.has_position = true;
      node.latitude_e7 = traffic.latitude_e7;
      node.longitude_e7 = traffic.longitude_e7;
    }
    return index;
  }

  // Gives a node a display name from a protocol that carries one.
  void name_node(uint32_t id, const char* name) {
    if (!name || !*name) return;
    for (Node& node : nodes_)
      if (node.id == id) {
        std::strncpy(node.name, name, sizeof(node.name) - 1);
        node.name[sizeof(node.name) - 1] = '\0';
        return;
      }
  }

  // Nodes with a position, nearest first, capped at `wanted`.
  std::vector<size_t> nearest(double latitude, double longitude, size_t wanted) const {
    std::vector<std::pair<float, size_t>> ranked;
    for (size_t index = 0; index < nodes_.size(); ++index) {
      const Node& node = nodes_[index];
      if (!node.has_position) continue;
      const float km = float(distance_km(latitude, longitude, node.latitude(), node.longitude()));
      ranked.emplace_back(km, index);
    }
    const size_t take = std::min(wanted, ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + take, ranked.end());
    std::vector<size_t> found;
    found.reserve(take);
    for (size_t n = 0; n < take; ++n) found.push_back(ranked[n].second);
    return found;
  }

  // Pairs of positioned nodes within `radius_km` of each other: the links the
  // map draws. A mesh that spans a country would otherwise draw a hairball, so
  // the radius is the map's most important parameter.
  struct Link {
    size_t a = 0, b = 0;
    float km = 0;
  };

  std::vector<Link> links(double radius_km) const {
    std::vector<Link> found;
    for (size_t a = 0; a < nodes_.size(); ++a) {
      if (!nodes_[a].has_position) continue;
      for (size_t b = a + 1; b < nodes_.size(); ++b) {
        if (!nodes_[b].has_position) continue;
        const double km = distance_km(nodes_[a].latitude(), nodes_[a].longitude(),
                                      nodes_[b].latitude(), nodes_[b].longitude());
        if (km <= radius_km) found.push_back({a, b, float(km)});
      }
    }
    return found;
  }

 private:
  size_t weakest_index() const {
    size_t weakest = 0;
    for (size_t index = 1; index < nodes_.size(); ++index)
      if (nodes_[index].packets < nodes_[weakest].packets) weakest = index;
    return weakest;
  }

  static void format_fallback_name(Node& node) {
    std::snprintf(node.name, sizeof(node.name), "!%08X", node.id);
  }

  std::vector<Node> nodes_;
};

}  // namespace orc::zero::mesh
