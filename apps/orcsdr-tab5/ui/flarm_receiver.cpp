#include "flarm_receiver.hpp"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace orcsdr::flarm {
namespace {
struct Track { flarm_rx::Frame frame{}; uint32_t seen = 0; bool used = false; bool confirmed = false; };
struct State {
  flarm_rx::Decoder decoder;
  Track tracks[64]{};
  flarm_rx::Context reference{};
  flarm_rx::Stats stats{};
  uint32_t generation = 0, sequence = 0, now = 0, revision = 0;
};
State* state = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
void on_frame(const flarm_rx::Frame& f, void*) {
  portENTER_CRITICAL(&mux);
  Track* target = nullptr;
  Track* oldest = &state->tracks[0];
  for (auto& t : state->tracks) {
    if (t.used && t.frame.address == f.address && t.frame.address_type == f.address_type) {
      target = &t; break;
    }
    if (!t.used && !target) target = &t;
    if (uint32_t(state->now - t.seen) > uint32_t(state->now - oldest->seen)) oldest = &t;
  }
  if (!target) target = oldest;
  bool confirmed = f.generation == 7;
  if (target->used && target->frame.address == f.address && target->frame.address_type == f.address_type) {
    const uint32_t age = state->now - target->seen;
    const double dy = (f.latitude - target->frame.latitude) * 111320;
    const double dx = std::remainder(f.longitude - target->frame.longitude, 360.0) * 111320 *
                      std::cos(f.latitude * 0.017453292519943295);
    // Two distinct broadcasts must agree before V6 is displayed. A 16-bit RF
    // CRC validates ciphertext, while the old plaintext has only one parity bit.
    if (age >= 100 && age <= 5000 && std::hypot(dx, dy) < 250 + age * 0.4 &&
        std::fabs(f.altitude_m - target->frame.altitude_m) < 200 + age * 0.1)
      confirmed = true;
    else if (age < 100) confirmed = target->confirmed;
  }
  *target = {f, state->now, true, confirmed};
  portEXIT_CRITICAL(&mux);
}
}
bool initialize() {
  if (state) return true;
  void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return false;
  state = new (memory) State;
  return true;
}
void set_reference(bool valid, int32_t lat, int32_t lon) {
  if (!state) return;
  portENTER_CRITICAL(&mux);
  state->reference.location_valid = valid;
  state->reference.latitude_e7 = lat;
  state->reference.longitude_e7 = lon;
  portEXIT_CRITICAL(&mux);
}
void process(const uint8_t* iq, size_t n, uint32_t generation, uint32_t sequence,
             uint64_t unix_ms, uint32_t now_ms) {
  if (!state) return;
  if (state->generation != generation) {
    state->decoder.reset();
    portENTER_CRITICAL(&mux);
    for (auto& t : state->tracks) t = {};
    state->stats = {};
    state->generation = generation;
    portEXIT_CRITICAL(&mux);
  } else if (sequence != state->sequence + 1) state->decoder.discontinuity();
  state->sequence = sequence;
  state->now = now_ms;
  portENTER_CRITICAL(&mux);
  auto context = state->reference;
  portEXIT_CRITICAL(&mux);
  context.unix_ms = unix_ms;
  state->decoder.process_cu8(iq, n, context, on_frame, nullptr);
  portENTER_CRITICAL(&mux);
  state->stats = state->decoder.stats();
  ++state->revision;
  portEXIT_CRITICAL(&mux);
}
adsb::Snapshot snapshot(uint32_t generation, uint32_t now_ms) {
  adsb::Snapshot out{};
  out.strongest_signal_dbfs = -120;
  if (!state) return out;
  // Only copy the six freshest targets while holding the short critical section.
  Track visible[adsb::kVisibleAircraft]{};
  portENTER_CRITICAL(&mux);
  if (generation == state->generation) {
    out.flarm_v6 = state->stats.v6; out.flarm_v7 = state->stats.v7;
    out.flarm_crc_errors = state->stats.crc_errors;
    out.total_messages = state->stats.v6 + state->stats.v7;
    out.revision = state->revision;
    for (const auto& t : state->tracks) {
      if (!t.used || !t.confirmed || now_ms - t.seen >= 30000) continue;
      ++out.aircraft_count;
      out.strongest_signal_dbfs = std::max(out.strongest_signal_dbfs, t.frame.signal_dbfs);
      for (size_t i = 0; i < adsb::kVisibleAircraft; ++i) {
        if (!visible[i].used || now_ms - t.seen < now_ms - visible[i].seen) {
          for (size_t j = adsb::kVisibleAircraft - 1; j > i; --j) visible[j] = visible[j - 1];
          visible[i] = t; break;
        }
      }
    }
  }
  portEXIT_CRITICAL(&mux);
  for (const auto& t : visible) {
    if (!t.used) break;
    const auto& f = t.frame;
    auto& a = out.aircraft[out.visible_count++];
    a.icao = f.address; a.address_type = f.address_type;
    a.protocol_generation = f.generation; a.channel = f.channel; a.age_ms = now_ms - t.seen;
    std::snprintf(a.callsign, sizeof(a.callsign), "%06lX", static_cast<unsigned long>(f.address));
    std::snprintf(a.type, sizeof(a.type), "%s", flarm_rx::aircraft_type_name(f.aircraft_type));
    std::snprintf(a.owner, sizeof(a.owner), "AIR V%u%s%s", f.generation,
                  f.stealth ? " / STEALTH" : "", f.no_track ? " / NO TRACK" : "");
    a.has_callsign = true;
    a.latitude = float(f.latitude); a.longitude = float(f.longitude);
    a.altitude_ft = int(std::lround(f.altitude_m * 3.28084f));
    a.speed_kts = int(std::lround(f.speed_mps * 1.943844f));
    a.heading_deg = int(std::lround(f.course_deg)) % 360;
    a.vertical_rate_fpm = int(std::lround(f.climb_mps * 196.8504f));
    a.signal_dbfs = f.signal_dbfs;
    a.has_position = a.has_altitude = a.has_speed = a.has_heading = a.has_vertical_rate = true;
  }
  return out;
}
} // namespace orcsdr::flarm
