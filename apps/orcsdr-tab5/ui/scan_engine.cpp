#include "scan_engine.hpp"

namespace orcsdr::scan {

bool Engine::start(const Plan& plan, uint32_t restore_hz, uint32_t now_ms) {
  if (active_ || plan.count == 0 ||
      (plan.mode == Mode::channel_list && plan.channels_hz == nullptr) ||
      (plan.mode != Mode::channel_list && (plan.start_hz == 0 || plan.step_hz == 0)))
    return false;
  plan_ = plan;
  restore_hz_ = restore_hz;
  due_ms_ = now_ms;
  index_ = 0;
  active_ = true;
  tune_pending_ = true;
  return true;
}

uint32_t Engine::target(size_t index) const {
  return plan_.mode == Mode::channel_list
             ? plan_.channels_hz[index]
             : plan_.start_hz + static_cast<uint32_t>(index) * plan_.step_hz;
}

void Engine::service(uint32_t now_ms, const Callbacks& callbacks) {
  if (!active_ || callbacks.retune == nullptr || callbacks.measure == nullptr) return;
  if (tune_pending_) {
    if (!callbacks.retune(target(index_), callbacks.context)) {
      finish(Finish::retune_failed, true, callbacks);
      return;
    }
    tune_pending_ = false;
    due_ms_ = now_ms + plan_.settle_ms;
    return;
  }
  if (static_cast<int32_t>(now_ms - due_ms_) < 0) return;
  callbacks.measure(index_, target(index_), callbacks.context);
  if (++index_ == plan_.count) {
    finish(Finish::completed, plan_.restore, callbacks);
    return;
  }
  tune_pending_ = true;
}

void Engine::finish(Finish reason, bool restore, const Callbacks& callbacks) {
  if (restore && restore_hz_ != 0 && callbacks.retune != nullptr)
    (void)callbacks.retune(restore_hz_, callbacks.context);
  active_ = false;
  tune_pending_ = false;
  if (callbacks.finished != nullptr) callbacks.finished(reason, callbacks.context);
}

void Engine::cancel(bool restore, const Callbacks& callbacks) {
  if (active_) finish(Finish::cancelled, restore && plan_.restore, callbacks);
}

Progress Engine::progress() const {
  return {active_, index_, plan_.count, active_ ? target(index_) : 0};
}

namespace {
struct Check {
  uint32_t tuned[8]{};
  size_t tunes = 0;
  size_t measures = 0;
  Finish finish = Finish::retune_failed;
  bool fail_retune = false;
};
bool check_retune(uint32_t hz, void* context) {
  auto& check = *static_cast<Check*>(context);
  check.tuned[check.tunes++] = hz;
  return !check.fail_retune;
}
void check_measure(size_t, uint32_t, void* context) {
  ++static_cast<Check*>(context)->measures;
}
void check_finished(Finish finish, void* context) {
  static_cast<Check*>(context)->finish = finish;
}
}  // namespace

bool Engine::self_check() {
  const uint32_t channels[] = {100, 200};
  Check check;
  const Callbacks callbacks{check_retune, check_measure, check_finished, &check};
  Engine engine;
  if (engine.start({}, 50, 0)) return false;
  if (!engine.start({Mode::channel_list, channels, 2, 0, 0, 10, true}, 50, 100))
    return false;
  engine.service(100, callbacks);
  Progress progress = engine.progress();
  if (!progress.active || progress.index != 0 || progress.count != 2 ||
      progress.frequency_hz != 100)
    return false;
  engine.service(109, callbacks);
  engine.service(110, callbacks);
  progress = engine.progress();
  if (!progress.active || progress.index != 1 || progress.frequency_hz != 200)
    return false;
  engine.service(110, callbacks);
  engine.service(120, callbacks);
  if (engine.active() || check.measures != 2 || check.tunes != 3 ||
      check.tuned[0] != 100 || check.tuned[1] != 200 || check.tuned[2] != 50 ||
      check.finish != Finish::completed)
    return false;

  check = {};
  if (!engine.start({Mode::frequency_range, nullptr, 1, 300, 25, 0, true}, 75, 0))
    return false;
  engine.service(0, callbacks);
  engine.cancel(true, callbacks);
  if (check.tunes != 2 || check.tuned[0] != 300 || check.tuned[1] != 75 ||
      check.finish != Finish::cancelled)
    return false;

  check = {};
  check.fail_retune = true;
  if (!engine.start({Mode::window_sweep, nullptr, 1, 400, 50, 0, true}, 80, 0))
    return false;
  engine.service(0, callbacks);
  return !engine.active() && check.measures == 0 && check.finish == Finish::retune_failed;
}

}  // namespace orcsdr::scan
