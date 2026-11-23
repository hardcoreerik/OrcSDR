// rtl_tcp_probe: check a real rtl_tcp server with the same client code the Tab5 runs.
//
//   g++ -std=c++17 -O2 -pthread -I../ui rtl_tcp_probe.cpp ../ui/rtl_tcp_session.cpp \
//       ../ui/rtl_tcp_protocol.cpp -o rtl_tcp_probe
//   ./rtl_tcp_probe <host> [port] [frequency_hz] [sample_rate_sps] [seconds]
//
// It connects, validates the greeting, tunes, receives for N seconds and prints the
// measured byte rate and basic IQ statistics. It sends only tuning commands and never
// enables the bias tee. Exit code 0 means IQ was received.

#include "rtl_tcp_session.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace orcsdr::rtl_tcp;

class ProbeEnv : public Env {
 public:
  ProbeEnv(uint32_t freq, uint32_t rate) : freq_(freq), rate_(rate) {}
  bool acquire(uint8_t** slot, size_t* capacity) override {
    *slot = buffer_.data();
    *capacity = buffer_.size();
    return true;
  }
  void submit(uint8_t* slot, size_t bytes) override {
    for (size_t i = 0; i + 1 < bytes; i += 2) {
      const double iv = static_cast<double>(slot[i]) - 127.5;
      const double qv = static_cast<double>(slot[i + 1]) - 127.5;
      sum_i_ += iv;
      sum_q_ += qv;
      power_ += iv * iv + qv * qv;
      if (slot[i] == 0 || slot[i] == 255 || slot[i + 1] == 0 || slot[i + 1] == 255) ++clipped_;
      ++samples_;
    }
  }
  void release(uint8_t*) override {}
  void discontinuity() override { ++discontinuities_; }
  bool pop_command(uint8_t*) override { return false; }
  Desired desired() override {
    Desired d;
    d.frequency_hz = freq_;
    d.sample_rate_sps = rate_;
    return d;
  }
  bool stop_requested() override { return stop_.load(); }
  void on_state(State state, EndReason reason) override {
    std::printf("state=%s%s%s\n", state_name(state), reason == EndReason::none ? "" : " reason=",
                reason == EndReason::none ? "" : end_reason_name(reason));
  }
  uint32_t now_ms() override {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }
  void sleep_ms(uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

  std::atomic<bool> stop_{false};
  uint64_t samples_ = 0, clipped_ = 0;
  double sum_i_ = 0, sum_q_ = 0, power_ = 0;
  int discontinuities_ = 0;

 private:
  uint32_t freq_, rate_;
  std::vector<uint8_t> buffer_ = std::vector<uint8_t>(32768);
};

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <host> [port] [frequency_hz] [sample_rate_sps] [seconds]\n", argv[0]);
    return 2;
  }
  SessionConfig config;
  if (!valid_host(argv[1])) {
    std::fprintf(stderr, "invalid host\n");
    return 2;
  }
  std::snprintf(config.host, sizeof(config.host), "%s", argv[1]);
  config.port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : kDefaultPort;
  const uint32_t freq = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 100100000u;
  const uint32_t rate = argc > 4 ? static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 960000u;
  const int seconds = argc > 5 ? std::atoi(argv[5]) : 10;
  config.backoff = BackoffPolicy{500, 2000, 2};

  ProbeEnv env(freq, rate);
  Stats stats;
  std::thread worker([&] { run(config, env, stats); });
  const auto start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  env.stop_ = true;
  worker.join();

  const double bytes = static_cast<double>(stats.bytes_received.load());
  std::printf("server=%s:%u tuner=%s (%u) gains=%u\n", config.host, config.port,
              tuner_type_name(stats.tuner_type_raw.load()), stats.tuner_type_raw.load(),
              stats.gain_count.load());
  std::printf("requested_rate=%u freq=%u elapsed_s=%.2f bytes=%.0f rate_MBps=%.3f effective_MSps=%.3f\n",
              rate, freq, elapsed, bytes, bytes / elapsed / 1e6, bytes / 2.0 / elapsed / 1e6);
  if (env.samples_ > 0) {
    const double n = static_cast<double>(env.samples_);
    std::printf("samples=%llu mean_i=%.2f mean_q=%.2f rms=%.2f clipped_pct=%.4f dropped=%llu discontinuities=%d\n",
                static_cast<unsigned long long>(env.samples_), env.sum_i_ / n, env.sum_q_ / n,
                std::sqrt(env.power_ / n), 100.0 * static_cast<double>(env.clipped_) / (2.0 * n),
                static_cast<unsigned long long>(stats.bytes_dropped.load()), env.discontinuities_);
  }
  std::printf("PROBE %s\n", env.samples_ > 0 ? "OK" : "NO_IQ");
  return env.samples_ > 0 ? 0 : 1;
}
