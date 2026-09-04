#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "radio_session.hpp"
#include "scan_engine.hpp"

namespace {

std::atomic_size_t allocations{0};

struct Trace {
  std::array<uint32_t, 64> tunes{};
  std::array<uint32_t, 64> measured_hz{};
  std::array<size_t, 64> measured_index{};
  size_t tune_count = 0;
  size_t measure_count = 0;
  size_t finish_count = 0;
  orcsdr::scan::Finish finish = orcsdr::scan::Finish::retune_failed;
  uint32_t fail_hz = 0;
  orcsdr::radio::Session* session = nullptr;
  orcsdr::radio::Token token{};
};

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}

#define CHECK(expression) \
  do {                    \
    if (!(expression)) fail(#expression, __LINE__); \
  } while (false)

bool retune(uint32_t frequency_hz, void* context) {
  auto& trace = *static_cast<Trace*>(context);
  CHECK(trace.tune_count < trace.tunes.size());
  trace.tunes[trace.tune_count++] = frequency_hz;
  if (frequency_hz == trace.fail_hz) return false;
  return trace.session == nullptr || trace.session->retuned(trace.token, frequency_hz);
}

void measure(size_t index, uint32_t frequency_hz, void* context) {
  auto& trace = *static_cast<Trace*>(context);
  CHECK(trace.measure_count < trace.measured_hz.size());
  trace.measured_index[trace.measure_count] = index;
  trace.measured_hz[trace.measure_count++] = frequency_hz;
}

void finished(orcsdr::scan::Finish reason, void* context) {
  auto& trace = *static_cast<Trace*>(context);
  trace.finish = reason;
  ++trace.finish_count;
}

orcsdr::scan::Callbacks callbacks(Trace& trace) {
  return {retune, measure, finished, &trace};
}

void test_radio_session() {
  using namespace orcsdr::radio;
  Session session;
  CHECK(session.snapshot().owner == Owner::none);
  CHECK(!session.owns(session.acquire(Owner::none, Band::fm, 100, 200)));
  CHECK(session.snapshot().generation == 0);

  const Token fm = session.acquire(Owner::fm, Band::fm, 96100000, 960000);
  CHECK(session.owns(fm));
  CHECK(session.set_state(fm, ReceiverState::running));
  CHECK(!session.retuned(fm, 0));
  CHECK(session.retuned(fm, 101700000));
  const Snapshot fm_snapshot = session.snapshot();
  CHECK(fm_snapshot.owner == Owner::fm);
  CHECK(fm_snapshot.band == Band::fm);
  CHECK(fm_snapshot.state == ReceiverState::running);
  CHECK(fm_snapshot.frequency_hz == 101700000);
  CHECK(fm_snapshot.sample_rate_sps == 960000);

  const Token p25 = session.acquire(Owner::p25, Band::p25, 453812500, 960000);
  CHECK(!session.owns(fm));
  CHECK(!session.retuned(fm, 102300000));
  CHECK(!session.set_state(fm, ReceiverState::failed));
  CHECK(session.owns(p25));
  CHECK(session.snapshot().frequency_hz == 453812500);

  CHECK(owner_for_band(Band::fm) == Owner::fm);
  CHECK(owner_for_band(Band::p25) == Owner::p25);
  CHECK(owner_for_band(Band::adsb) == Owner::adsb);
  CHECK(owner_for_band(Band::lora) == Owner::lora);
  CHECK(owner_for_band(Band::am) == Owner::radio);
  CHECK(owner_for_band(Band::wx) == Owner::radio);
  CHECK(owner_for_band(Band::cb) == Owner::radio);
  CHECK(owner_for_band(Band::browse) == Owner::radio);
  CHECK(Session::self_check());
}

void test_scan_validation_and_timing() {
  using namespace orcsdr::scan;
  Engine engine;
  CHECK(!engine.start({}, 0, 0));
  CHECK(!engine.start({Mode::channel_list, nullptr, 1}, 0, 0));
  CHECK(!engine.start({Mode::frequency_range, nullptr, 1, 0, 10}, 0, 0));
  CHECK(!engine.start({Mode::window_sweep, nullptr, 1, 100, 0}, 0, 0));

  const uint32_t channels[] = {100, 200};
  Trace trace;
  const auto cb = callbacks(trace);
  constexpr uint32_t near_wrap = UINT32_MAX - 5;
  CHECK(engine.start({Mode::channel_list, channels, 2, 0, 0, 10, true}, 50,
                     near_wrap));
  CHECK(!engine.start({Mode::channel_list, channels, 2}, 0, 0));
  engine.service(near_wrap, cb);
  CHECK(trace.tune_count == 1 && trace.tunes[0] == 100);
  CHECK(trace.measure_count == 0);
  engine.service(UINT32_MAX, cb);
  engine.service(3, cb);
  CHECK(trace.measure_count == 0);
  engine.service(4, cb);
  CHECK(trace.measure_count == 1 && trace.measured_index[0] == 0 &&
        trace.measured_hz[0] == 100);
  CHECK(engine.progress().index == 1 && engine.progress().frequency_hz == 200);
  engine.service(4, cb);
  engine.service(13, cb);
  CHECK(trace.measure_count == 1);
  engine.service(14, cb);
  CHECK(!engine.active());
  CHECK(trace.measure_count == 2 && trace.measured_index[1] == 1 &&
        trace.measured_hz[1] == 200);
  CHECK(trace.tune_count == 3 && trace.tunes[2] == 50);
  CHECK(trace.finish_count == 1 && trace.finish == Finish::completed);
  CHECK(!engine.progress().active && engine.progress().index == 2 &&
        engine.progress().frequency_hz == 0);
}

void test_scan_modes_cancel_and_failures() {
  using namespace orcsdr::scan;
  for (const Mode mode : {Mode::frequency_range, Mode::window_sweep}) {
    Engine engine;
    Trace trace;
    const auto cb = callbacks(trace);
    CHECK(engine.start({mode, nullptr, 3, 300, 25, 0, false}, 75, 0));
    for (size_t i = 0; i < 3; ++i) {
      engine.service(0, cb);
      engine.service(0, cb);
    }
    CHECK(!engine.active());
    CHECK(trace.tune_count == 3 && trace.tunes[0] == 300 && trace.tunes[1] == 325 &&
          trace.tunes[2] == 350);
    CHECK(trace.measure_count == 3 && trace.finish == Finish::completed);
  }

  const uint32_t one[] = {400};
  Engine engine;
  Trace trace;
  auto cb = callbacks(trace);
  CHECK(engine.start({Mode::channel_list, one, 1, 0, 0, 0, true}, 80, 0));
  engine.service(0, cb);
  engine.cancel(true, cb);
  CHECK(!engine.active() && trace.tune_count == 2 && trace.tunes[1] == 80 &&
        trace.finish == Finish::cancelled);

  trace = {};
  cb = callbacks(trace);
  CHECK(engine.start({Mode::channel_list, one, 1, 0, 0, 0, true}, 80, 0));
  engine.service(0, cb);
  engine.cancel(false, cb);
  CHECK(trace.tune_count == 1 && trace.finish == Finish::cancelled);

  const uint32_t two[] = {400, 500};
  trace = {};
  trace.fail_hz = 500;
  cb = callbacks(trace);
  CHECK(engine.start({Mode::channel_list, two, 2, 0, 0, 0, true}, 80, 0));
  engine.service(0, cb);
  engine.service(0, cb);
  engine.service(0, cb);
  CHECK(!engine.active() && trace.measure_count == 1 && trace.tune_count == 3 &&
        trace.tunes[2] == 80 && trace.finish == Finish::retune_failed);

  trace = {};
  trace.fail_hz = 80;
  cb = callbacks(trace);
  CHECK(engine.start({Mode::channel_list, one, 1, 0, 0, 0, true}, 80, 0));
  engine.service(0, cb);
  engine.service(0, cb);
  CHECK(!engine.active() && trace.measure_count == 1 &&
        trace.finish == Finish::retune_failed);
  CHECK(Engine::self_check());
}

void test_session_scan_ownership() {
  using namespace orcsdr;
  const uint32_t channels[] = {1100, 1200};
  radio::Session session;
  scan::Engine engine;
  Trace trace;
  trace.session = &session;
  trace.token = session.acquire(radio::Owner::fm, radio::Band::fm, 1000, 960000);
  auto cb = callbacks(trace);
  CHECK(engine.start({scan::Mode::channel_list, channels, 2, 0, 0, 0, true}, 1000, 0));
  engine.service(0, cb);
  engine.service(0, cb);
  engine.service(0, cb);
  engine.service(0, cb);
  CHECK(trace.finish == scan::Finish::completed);
  CHECK(session.snapshot().frequency_hz == 1000);

  trace = {};
  trace.session = &session;
  trace.token = session.acquire(radio::Owner::fm, radio::Band::fm, 1000, 960000);
  cb = callbacks(trace);
  CHECK(engine.start({scan::Mode::channel_list, channels, 2, 0, 0, 0, true}, 1000, 0));
  engine.service(0, cb);
  CHECK(session.snapshot().frequency_hz == 1100);
  const auto p25 = session.acquire(radio::Owner::p25, radio::Band::p25, 453812500, 960000);
  engine.cancel(false, cb);
  CHECK(trace.finish == scan::Finish::cancelled && trace.tune_count == 1);
  CHECK(session.owns(p25) && session.snapshot().frequency_hz == 453812500);
}

void test_allocation_free_stress() {
  using namespace orcsdr::scan;
  const uint32_t channel[] = {100};
  Engine engine;
  Trace trace;
  const auto cb = callbacks(trace);
  const size_t before = allocations.load(std::memory_order_relaxed);
  for (size_t cycle = 0; cycle < 10000; ++cycle) {
    trace = {};
    CHECK(engine.start({Mode::channel_list, channel, 1, 0, 0, 0, true}, 50, 0));
    engine.service(0, cb);
    engine.service(0, cb);
    CHECK(trace.finish == Finish::completed);
  }
  CHECK(allocations.load(std::memory_order_relaxed) == before);
}

}  // namespace

void* operator new(std::size_t size) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
  test_radio_session();
  test_scan_validation_and_timing();
  test_scan_modes_cancel_and_failures();
  test_session_scan_ownership();
  test_allocation_free_stress();
  std::puts("radio_scan_tests: PASS");
  return 0;
}
