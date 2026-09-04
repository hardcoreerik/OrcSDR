#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::scan {

enum class Mode : uint8_t { channel_list, frequency_range, window_sweep };
enum class Finish : uint8_t { completed, cancelled, retune_failed };

struct Plan {
  Mode mode = Mode::channel_list;
  const uint32_t* channels_hz = nullptr;
  size_t count = 0;
  uint32_t start_hz = 0;
  uint32_t step_hz = 0;
  uint32_t settle_ms = 0;
  bool restore = true;
};

struct Progress {
  bool active = false;
  size_t index = 0;
  size_t count = 0;
  uint32_t frequency_hz = 0;
};

using Retune = bool (*)(uint32_t frequency_hz, void* context);
using Measure = void (*)(size_t index, uint32_t frequency_hz, void* context);
using Finished = void (*)(Finish finish, void* context);

struct Callbacks {
  Retune retune = nullptr;
  Measure measure = nullptr;
  Finished finished = nullptr;
  void* context = nullptr;
};

class Engine {
 public:
  bool start(const Plan& plan, uint32_t restore_hz, uint32_t now_ms);
  void service(uint32_t now_ms, const Callbacks& callbacks);
  void cancel(bool restore, const Callbacks& callbacks);
  Progress progress() const;
  bool active() const { return active_; }
  static bool self_check();

 private:
  uint32_t target(size_t index) const;
  void finish(Finish reason, bool restore, const Callbacks& callbacks);

  Plan plan_{};
  uint32_t restore_hz_ = 0;
  uint32_t due_ms_ = 0;
  size_t index_ = 0;
  bool active_ = false;
  bool tune_pending_ = false;
};

}  // namespace orcsdr::scan
