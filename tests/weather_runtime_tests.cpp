#include "weather_runtime.hpp"
#include <cstdio>
#include <cstdlib>

[[noreturn]] static void fail(const char* e, int l) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", l, e);
  std::exit(1);
}
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (false)

int main() {
  using namespace orcsdr::weather;
  Runtime runtime;
  CHECK(runtime.enter().kind == ReceiverCommandKind::none);
  CHECK(runtime.select_channel(6));
  auto command = runtime.listen();
  CHECK(command.kind == ReceiverCommandKind::start_foreground);
  CHECK(command.frequency_hz == 162550000u);
  CHECK(runtime.stop().kind == ReceiverCommandKind::stop_owned);

  CHECK(runtime.select_channel(0));
  command = runtime.scan(1000);
  CHECK(command.kind == ReceiverCommandKind::start_foreground);
  CHECK(command.frequency_hz == 162400000u);
  for (size_t i = 0; i < noaa::kChannelCount; ++i)
    runtime.record_scan_sample(-80.0f + static_cast<float>(i),
                               1400 + static_cast<uint32_t>(i) * 400);
  CHECK(runtime.state().scan_complete);
  CHECK(runtime.state().scan_samples == 7);
  command = runtime.finish_scan(true);
  CHECK(command.kind == ReceiverCommandKind::retune_owned);
  CHECK(command.frequency_hz == 162550000u);
  std::puts("weather_runtime_tests: PASS");
}
