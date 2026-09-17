#pragma once

#include "web_console.hpp"
#include <cstring>
#include <string_view>

namespace orcsdr::web_console {

inline bool parse_command(std::string_view body, Command& out) {
  out = {};
  struct Named { std::string_view name; CommandKind kind; };
  constexpr Named names[] = {
      {"volume_down", CommandKind::volume_down}, {"volume_up", CommandKind::volume_up},
      {"sound_toggle", CommandKind::sound_toggle}, {"span_down", CommandKind::span_down},
      {"span_up", CommandKind::span_up}, {"step_down", CommandKind::step_down},
      {"step_up", CommandKind::step_up}};
  for (const auto& item : names) {
    if (body == item.name) { out.kind = item.kind; return true; }
  }
  if (body.substr(0, 5) == "tune=") {
    const auto digits = body.substr(5);
    if (digits.empty()) return false;
    uint32_t hz = 0;
    for (const char digit : digits) {
      if (digit < '0' || digit > '9') return false;
      const uint32_t n = static_cast<uint32_t>(digit - '0');
      if (hz > (1766000000u - n) / 10u) return false;
      hz = hz * 10u + n;
    }
    if (hz < 24000) return false;
    out.kind = CommandKind::tune;
    out.value = hz;
    return true;
  }
  if (body.substr(0, 5) == "open=") {
    const auto id = body.substr(5);
    // Only destinations currently handled by main's LAN-command adapter.
    constexpr std::string_view ids[] = {"home", "fm", "am", "p25", "adsb",
        "shortwave", "weather", "cb", "lora", "airband", "marine", "satellite",
        "rf_lab", "settings"};
    for (const auto known : ids) {
      if (id != known) continue;
      out.kind = CommandKind::open;
      std::memcpy(out.id, id.data(), id.size());
      return true;
    }
  }
  return false;
}

// Browser Origin is checked when present. Missing Origin is handled separately
// by the HTTP adapter for existing non-browser LAN clients; this is not auth.
inline bool same_origin(std::string_view origin, std::string_view host,
                        std::string_view wifi_ip) {
  if (host != "orcsdr.local" && (wifi_ip.empty() || host != wifi_ip)) return false;
  return origin.size() == host.size() + 7 && origin.substr(0, 7) == "http://" &&
         origin.substr(7) == host;
}

// Caller serializes submit/take under the existing web-console mutex.
class CommandSlot {
 public:
  bool submit(const Command& command) {
    if (pending_.kind != CommandKind::none || command.kind == CommandKind::none) return false;
    pending_ = command;
    return true;
  }
  bool take(Command& out) {
    if (pending_.kind == CommandKind::none) return false;
    out = pending_;
    pending_ = {};
    return true;
  }
 private:
  Command pending_{};
};

}  // namespace orcsdr::web_console
