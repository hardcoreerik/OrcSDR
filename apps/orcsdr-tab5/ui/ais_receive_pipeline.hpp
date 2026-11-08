#pragma once
// OrcSDR AIS receive pipeline: radio-owned CU8 -> CRC-valid packet ->
// original field parser -> bounded station store. NOT a USB/radio owner.
// Caller guarantees single DSP-task ownership and supplies radio session generation.
// UI workers must consume separately published snapshots, never the mutable store.
#include "ais_receiver.hpp"
#include "ais_protocol.hpp"
#include "ais_binary_app.hpp"
#include "ais_station_store.hpp"
#include <cstddef>
#include <cstdint>

namespace orcsdr::ais_rx {
struct PipelineStats {
  Stats rf{};
  uint64_t packets = 0;
  uint64_t messages_decoded = 0;
  uint64_t messages_header_only = 0;
  uint64_t binary_known = 0;
  uint64_t binary_preserved_unknown = 0;
  uint64_t binary_unstructured = 0;
  uint64_t binary_truncated = 0;
  uint64_t parse_rejected = 0;
  uint64_t station_updates = 0;
  uint64_t station_rejected = 0;
  uint64_t input_refused = 0;
  uint64_t generation_changes = 0;
  uint64_t iq_gaps = 0;
};
class Pipeline {
 public:
  Pipeline() = default;
  using BinaryObserver = void (*)(const ais::app::Result&, void*);
  // Inline on owning DSP task, only for CRC-validated Type 6/8/25/26 packets.
  // Do not block, draw or allocate in the observer. Result lifetime is callback only.
  void set_binary_observer(BinaryObserver cb,void* context) {
    binary_observer_=cb;binary_observer_ctx_=context;
  }
  // Only feed blocks belonging to the active 162MHz/240kS/s radio session.
  // Explicit generation is checked to avoid decoding old blocks after retune.
  bool offer(const uint8_t* cu8, size_t bytes, uint32_t sample_rate,
             uint32_t center_hz, uint32_t source_generation, uint64_t now_ms);
  void discontinuity();
  void clear();
  PipelineStats stats() const;
  const ais::StationStore& stations() const { return stations_; }
 private:
  static void on_frame(const Packet& packet, void* context);
  void accept(const Packet& packet);
  Receiver receiver_{};
  ais::StationStore stations_{};
  uint32_t source_generation_ = 0;
  uint64_t last_input_ms_ = 0, block_time_ms_ = 0;
  bool have_last_input_ = false;
  PipelineStats counters_{};
  BinaryObserver binary_observer_=nullptr;
  void* binary_observer_ctx_=nullptr;
};
} // namespace orcsdr::ais_rx
