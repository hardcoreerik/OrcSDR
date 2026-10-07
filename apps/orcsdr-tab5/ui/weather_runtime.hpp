#pragma once

#include "weather_service.hpp"

#include <cstdint>

namespace orcsdr::weather {

enum class ReceiverCommandKind : uint8_t {
  none,
  start_foreground,
  retune_owned,
  stop_owned,
};

struct ReceiverCommand {
  ReceiverCommandKind kind = ReceiverCommandKind::none;
  uint32_t frequency_hz = 0;
};

class Runtime {
 public:
  static constexpr uint32_t kScanDwellMs = 350;

  ReceiverCommand enter();
  ReceiverCommand leave();
  bool select_channel(uint8_t index);
  bool previous_channel();
  bool next_channel();
  ReceiverCommand listen();
  ReceiverCommand scan(uint32_t now_ms);
  ReceiverCommand stop();
  void record_scan_sample(float level_dbfs, uint32_t now_ms);\n  ReceiverCommand finish_scan(bool completed);
  const ServiceState& state() const { return service_.state(); }

 private:
  Service service_{};
  uint32_t scan_due_ms_ = 0;
};

}  // namespace orcsdr::weather
