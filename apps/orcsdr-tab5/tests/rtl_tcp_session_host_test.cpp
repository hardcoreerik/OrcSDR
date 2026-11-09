// End-to-end test of the rtl_tcp client loop against a fake server on loopback.
// This is a host simulation: it proves protocol and recovery behaviour, not
// Wi-Fi throughput or timing on the Tab5.

#include "rtl_tcp_session.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using namespace orcsdr::rtl_tcp;

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

static uint8_t pattern(uint64_t i) {
  return (i % 2 == 0) ? static_cast<uint8_t>(0x10 + (i / 2) % 100)
                      : static_cast<uint8_t>(0xC0 + (i / 2) % 50);
}

static bool wait_for(const std::function<bool()>& predicate, int timeout_ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < end) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

// ---------------------------------------------------------------- fake server
class FakeServer {
 public:
  using Handler = std::function<void(int fd, int index, FakeServer& self)>;

  explicit FakeServer(Handler handler, int max_connections = 1)
      : handler_(std::move(handler)), max_(max_connections) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t length = sizeof(addr);
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &length);
    port_ = ntohs(addr.sin_port);
    listen(listen_fd_, 4);
    thread_ = std::thread([this] { loop(); });
  }
  ~FakeServer() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    close(listen_fd_);
  }
  uint16_t port() const { return port_; }
  bool stopping() const { return stop_; }
  std::vector<uint8_t> commands() {
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_;
  }
  void read_commands(int fd) {
    uint8_t buf[64];
    const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      commands_.insert(commands_.end(), buf, buf + n);
    }
  }
  static bool write_all(int fd, const uint8_t* data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
      const ssize_t n = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
      if (n <= 0) return false;
      sent += static_cast<size_t>(n);
    }
    return true;
  }
  static void sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

 private:
  void loop() {
    int served = 0;
    while (!stop_ && served < max_) {
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(listen_fd_, &readable);
      timeval wait{0, 50 * 1000};
      if (select(listen_fd_ + 1, &readable, nullptr, nullptr, &wait) <= 0) continue;
      const int fd = accept(listen_fd_, nullptr, nullptr);
      if (fd < 0) continue;
      handler_(fd, served++, *this);
      close(fd);
    }
  }
  Handler handler_;
  int max_;
  int listen_fd_ = -1;
  uint16_t port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread thread_;
  std::mutex mutex_;
  std::vector<uint8_t> commands_;
};

static std::vector<uint8_t> greeting(uint32_t tuner, uint32_t gains) {
  return {'R', 'T', 'L', '0',
          static_cast<uint8_t>(tuner >> 24), static_cast<uint8_t>(tuner >> 16),
          static_cast<uint8_t>(tuner >> 8), static_cast<uint8_t>(tuner),
          static_cast<uint8_t>(gains >> 24), static_cast<uint8_t>(gains >> 16),
          static_cast<uint8_t>(gains >> 8), static_cast<uint8_t>(gains)};
}

// ------------------------------------------------------------------- test env
class TestEnv : public Env {
 public:
  static constexpr size_t kSlotBytes = 33280;
  explicit TestEnv(size_t slots = 3) : storage_(slots) {
    for (size_t i = 0; i < slots; ++i) free_.push_back(i);
    segments_.emplace_back();
  }
  bool acquire(uint8_t** slot, size_t* capacity) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (block_acquire.load() || free_.empty()) return false;
    const size_t index = free_.back();
    free_.pop_back();
    *slot = storage_[index].data();
    *capacity = kSlotBytes;
    return true;
  }
  void submit(uint8_t* slot, size_t bytes) override {
    std::lock_guard<std::mutex> lock(mutex_);
    segments_.back().insert(segments_.back().end(), slot, slot + bytes);
    give_back(slot);
  }
  void release(uint8_t* slot) override {
    std::lock_guard<std::mutex> lock(mutex_);
    give_back(slot);
  }
  void discontinuity() override {
    std::lock_guard<std::mutex> lock(mutex_);
    segments_.emplace_back();
    ++discontinuities;
  }
  bool pop_command(uint8_t out[kCommandBytes]) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.empty()) return false;
    std::memcpy(out, commands_.front().data(), kCommandBytes);
    commands_.erase(commands_.begin());
    return true;
  }
  Desired desired() override {
    Desired d;
    d.sample_rate_sps = 2400000;
    d.frequency_hz = 100100000;
    return d;
  }
  bool stop_requested() override { return stop.load(); }
  void on_state(State state, EndReason reason) override {
    std::lock_guard<std::mutex> lock(mutex_);
    states.push_back(state);
    reasons.push_back(reason);
  }
  uint32_t now_ms() override {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }
  void sleep_ms(uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

  void queue_command(Command command, uint32_t parameter) {
    std::array<uint8_t, kCommandBytes> bytes{};
    encode_command(command, parameter, bytes.data());
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push_back(bytes);
  }
  size_t delivered_total() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& s : segments_) total += s.size();
    return total;
  }
  std::vector<std::vector<uint8_t>> segments() {
    std::lock_guard<std::mutex> lock(mutex_);
    return segments_;
  }
  bool saw_state(State s) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto x : states) if (x == s) return true;
    return false;
  }
  bool saw_reason(EndReason r) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto x : reasons) if (x == r) return true;
    return false;
  }

  std::atomic<bool> stop{false};
  std::atomic<bool> block_acquire{false};
  std::atomic<int> discontinuities{0};
  std::vector<State> states;
  std::vector<EndReason> reasons;

 private:
  void give_back(uint8_t* slot) {
    for (size_t i = 0; i < storage_.size(); ++i)
      if (storage_[i].data() == slot) free_.push_back(i);
  }
  std::mutex mutex_;
  std::vector<std::array<uint8_t, kSlotBytes>> storage_;
  std::vector<size_t> free_;
  std::vector<std::vector<uint8_t>> segments_;
  std::vector<std::array<uint8_t, kCommandBytes>> commands_;
};

static SessionConfig fast_config(uint16_t port) {
  SessionConfig c;
  std::strcpy(c.host, "127.0.0.1");
  c.port = port;
  c.connect_timeout_ms = 1000;
  c.greeting_timeout_ms = 400;
  c.stall_timeout_ms = 1500;
  c.ring_wait_ms = 40;   // the old fixed wait: tests that block the ring on purpose expect a quick drop
  c.backoff = BackoffPolicy{10, 40, 3, 0};   // slow_ms 0: give up, so the tests that expect a final state end
  return c;
}

static bool pattern_prefix(const std::vector<uint8_t>& data) {
  for (size_t i = 0; i < data.size(); ++i)
    if (data[i] != pattern(i)) return false;
  return true;
}

// Streams `total` bytes of the pattern in awkward, odd-sized writes.
static void stream_pattern(int fd, size_t total, FakeServer& self) {
  uint32_t rng = 7;
  size_t sent = 0;
  std::vector<uint8_t> buf;
  while (sent < total && !self.stopping()) {
    rng = rng * 1664525u + 1013904223u;
    size_t chunk = 1 + (rng >> 8) % 5000;
    if (chunk > total - sent) chunk = total - sent;
    buf.resize(chunk);
    for (size_t i = 0; i < chunk; ++i) buf[i] = pattern(sent + i);
    if (!FakeServer::write_all(fd, buf.data(), chunk)) return;
    sent += chunk;
    self.read_commands(fd);
  }
}

static void test_normal_stream_and_commands() {
  const size_t total = 200000;
  FakeServer server([&](int fd, int, FakeServer& self) {
    const auto g = greeting(5, 29);
    FakeServer::write_all(fd, g.data(), 3);  // greeting arrives in pieces
    FakeServer::sleep(20);
    FakeServer::write_all(fd, g.data() + 3, 5);
    FakeServer::sleep(20);
    FakeServer::write_all(fd, g.data() + 8, 4);
    stream_pattern(fd, total, self);
    for (int i = 0; i < 100 && !self.stopping(); ++i) {
      self.read_commands(fd);
      FakeServer::sleep(10);
    }
  });
  TestEnv env;
  Stats stats;
  const auto cfg = fast_config(server.port());
  State final_state = State::idle;
  std::thread session([&] { final_state = run(cfg, env, stats); });
  CHECK(wait_for([&] { return env.delivered_total() >= total - 1; }, 5000));
  env.queue_command(Command::set_frequency, 145800000u);  // live retune
  CHECK(wait_for([&] { return server.commands().size() >= 15; }, 3000));
  env.stop = true;
  session.join();
  CHECK(final_state == State::idle);
  const auto segments = env.segments();
  CHECK(segments.size() == 1);
  CHECK(segments[0].size() % 2 == 0);
  CHECK(pattern_prefix(segments[0]));  // every sample in order, I before Q
  CHECK(segments[0].size() >= total - 1);
  const auto commands = server.commands();
  const uint8_t expected[15] = {0x02, 0x00, 0x24, 0x9F, 0x00,   // sample rate 2.4 MS/s
                                0x01, 0x05, 0xF7, 0x67, 0xA0,   // 100.1 MHz
                                0x01, 0x08, 0xB0, 0xBB, 0x40};  // 145.8 MHz retune
  CHECK(commands.size() >= 15 && std::memcmp(commands.data(), expected, 15) == 0);
  CHECK(stats.tuner_type_raw.load() == 5 && stats.gain_count.load() == 29);
  CHECK(env.saw_state(State::connecting) && env.saw_state(State::streaming));
  CHECK(stats.bytes_dropped.load() == 0);
}

static void test_wrong_service_is_refused() {
  FakeServer server(
      [](int fd, int, FakeServer&) {
        const char* text = "HTTP/1.1 400 Bad Request\r\n\r\n";
        FakeServer::write_all(fd, reinterpret_cast<const uint8_t*>(text), std::strlen(text));
        FakeServer::sleep(100);
      },
      8);
  TestEnv env;
  Stats stats;
  const State final_state = run(fast_config(server.port()), env, stats);
  CHECK(final_state == State::failed);
  CHECK(env.saw_reason(EndReason::greeting_failed));
  CHECK(!env.saw_state(State::streaming));
  CHECK(env.delivered_total() == 0);
}

static void test_silent_and_truncated_servers() {
  {
    FakeServer silent([](int, int, FakeServer&) { FakeServer::sleep(1500); }, 8);
    TestEnv env;
    Stats stats;
    const auto started = std::chrono::steady_clock::now();
    CHECK(run(fast_config(silent.port()), env, stats) == State::failed);
    CHECK(env.saw_reason(EndReason::no_greeting));   // silence is reported as such, not as "not an rtl_tcp server"
    CHECK(!env.saw_reason(EndReason::greeting_failed));
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
  }
  {
    FakeServer truncated(
        [](int fd, int, FakeServer&) {
          const auto g = greeting(5, 29);
          FakeServer::write_all(fd, g.data(), 7);  // drops during the greeting
        },
        8);
    TestEnv env;
    Stats stats;
    CHECK(run(fast_config(truncated.port()), env, stats) == State::failed);
    CHECK(env.saw_reason(EndReason::greeting_failed));
    CHECK(!env.saw_state(State::streaming));
  }
}

// A server that greets and then never sends samples (rtl_tcp running on a dead USB handle did exactly this: 744 reconnects, one every
// 13 s, and never a give-up). The connection lasts as long as the stall timeout, so a back-off that resets on connection time alone
// treats every one of them as healthy and retries forever. Only a connection that carried data may reset the back-off.
static void test_greeting_without_samples_keeps_backing_off() {
  FakeServer server(
      [](int fd, int, FakeServer& self) {
        const auto g = greeting(5, 29);
        FakeServer::write_all(fd, g.data(), g.size());
        // No samples, held open until the client leaves (the server loop is single-threaded, so it must notice that promptly).
        for (int i = 0; i < 400 && !self.stopping(); ++i) {
          char sink[16];
          const ssize_t n = recv(fd, sink, sizeof(sink), MSG_DONTWAIT);
          if (n == 0) break;  // peer closed
          FakeServer::sleep(10);
        }
      },
      16);
  TestEnv env;
  Stats stats;
  auto cfg = fast_config(server.port());
  cfg.stall_timeout_ms = 2300;  // longer than the 2 s a connection needs to count as "healthy by time"
  State final_state = State::idle;
  std::thread session([&] { final_state = run(cfg, env, stats); });
  const bool gave_up = wait_for([&] { return final_state == State::failed; }, 25000);
  env.stop = true;
  session.join();
  CHECK(gave_up);
  CHECK(final_state == State::failed);
  CHECK(env.saw_reason(EndReason::stalled));
  CHECK(stats.reconnects.load() == 3);  // bounded by the policy, not endless
}

static void test_nothing_listening() {
  uint16_t dead_port = 0;
  {
    FakeServer probe([](int, int, FakeServer&) {}, 0);
    dead_port = probe.port();
  }  // closed again, so the port refuses connections
  TestEnv env;
  Stats stats;
  CHECK(run(fast_config(dead_port), env, stats) == State::failed);
  CHECK(env.saw_reason(EndReason::connect_failed));
  CHECK(stats.connections.load() == 0);
  CHECK(stats.reconnects.load() == 3);  // the back-off is bounded
}

// G13: with a slow interval the client keeps trying after the fast attempts, and connects as soon as the server is back.
static void test_keeps_retrying_and_recovers() {
  uint16_t port = 0;
  {
    FakeServer probe([](int, int, FakeServer&) {}, 0);
    port = probe.port();
  }  // closed: refuses connections
  TestEnv env;
  Stats stats;
  auto cfg = fast_config(port);
  cfg.backoff = BackoffPolicy{10, 20, 2, 30};   // two fast attempts, then one every 30 ms
  State final_state = State::idle;
  std::thread session([&] { final_state = run(cfg, env, stats); });
  const bool past_fast = wait_for([&] { return stats.reconnects.load() >= 8; }, 8000);   // far more than max_attempts
  CHECK(past_fast);
  CHECK(!env.saw_state(State::failed));
  env.stop = true;
  session.join();
  CHECK(final_state == State::idle);   // ended by stop, never by giving up
}

static void test_reconnect_has_no_stale_samples() {
  FakeServer server(
      [](int fd, int index, FakeServer& self) {
        const auto g = greeting(5, 29);
        FakeServer::write_all(fd, g.data(), g.size());
        if (index == 0) {
          stream_pattern(fd, 50001, self);  // odd length, then the link drops
        } else {
          stream_pattern(fd, 50000, self);
          for (int i = 0; i < 60 && !self.stopping(); ++i) FakeServer::sleep(10);
        }
      },
      2);
  TestEnv env;
  Stats stats;
  auto cfg = fast_config(server.port());
  cfg.backoff = BackoffPolicy{10, 40, 8};
  std::thread session([&] { run(cfg, env, stats); });
  CHECK(wait_for([&] { return env.discontinuities.load() == 1 && env.delivered_total() >= 99999; },
                 6000));
  env.stop = true;
  session.join();
  const auto segments = env.segments();
  CHECK(segments.size() == 2);
  if (segments.size() == 2) {
    CHECK(segments[0].size() == 50000);  // the odd tail byte died with the connection
    CHECK(pattern_prefix(segments[0]));
    CHECK(segments[1].size() >= 49998);
    CHECK(pattern_prefix(segments[1]));  // starts on a fresh I, not a stale Q
  }
  CHECK(stats.connections.load() == 2);
  CHECK(env.saw_state(State::reconnecting));
}

static void test_full_ring_drops_are_counted_and_aligned() {
  FakeServer server([](int fd, int, FakeServer& self) {
    const auto g = greeting(5, 29);
    FakeServer::write_all(fd, g.data(), g.size());
    stream_pattern(fd, 300000, self);
    for (int i = 0; i < 60 && !self.stopping(); ++i) FakeServer::sleep(10);
  });
  TestEnv env;
  env.block_acquire = true;  // the DSP ring is full
  Stats stats;
  const auto cfg = fast_config(server.port());
  std::thread session([&] { run(cfg, env, stats); });
  CHECK(wait_for([&] { return stats.bytes_dropped.load() > 100000; }, 5000));
  env.block_acquire = false;
  const size_t before = env.delivered_total();
  CHECK(wait_for([&] { return env.delivered_total() > before; }, 5000));
  env.stop = true;
  session.join();
  CHECK(stats.bytes_dropped.load() > 0);
  bool aligned = true;
  for (const auto& segment : env.segments())
    for (size_t i = 0; i < segment.size(); ++i)
      if ((i % 2 == 0) != (segment[i] < 0x80)) aligned = false;
  CHECK(aligned);  // after losing bytes, every delivered pair still starts on I
}

static void test_short_dsp_stall_loses_nothing() {
  FakeServer server([](int fd, int, FakeServer& self) {
    const auto g = greeting(5, 29);
    FakeServer::write_all(fd, g.data(), g.size());
    stream_pattern(fd, 200000, self);
    for (int i = 0; i < 60 && !self.stopping(); ++i) FakeServer::sleep(10);
  });
  TestEnv env;
  env.block_acquire = true;  // the DSP is busy for a moment
  Stats stats;
  auto cfg = fast_config(server.port());
  cfg.ring_wait_ms = 600;    // longer than the stall below
  std::thread session([&] { run(cfg, env, stats); });
  FakeServer::sleep(150);
  env.block_acquire = false;
  CHECK(wait_for([&] { return env.delivered_total() >= 150000; }, 5000));
  env.stop = true;
  session.join();
  CHECK(stats.bytes_dropped.load() == 0);  // the stall was absorbed, nothing was thrown away
}

static void test_slow_link_is_reported() {
  FakeServer server([](int fd, int, FakeServer& self) {
    const auto g = greeting(5, 29);
    FakeServer::write_all(fd, g.data(), g.size());
    std::vector<uint8_t> chunk(10000);
    uint64_t sent = 0;
    while (!self.stopping()) {  // about 100 KB/s, against 4.8 MB/s needed
      for (size_t i = 0; i < chunk.size(); ++i) chunk[i] = pattern(sent + i);
      if (!FakeServer::write_all(fd, chunk.data(), chunk.size())) return;
      sent += chunk.size();
      FakeServer::sleep(100);
    }
  });
  TestEnv env;
  Stats stats;
  const auto cfg = fast_config(server.port());
  std::thread session([&] { run(cfg, env, stats); });
  CHECK(wait_for([&] { return stats.link_ratio_pct.load() > 0; }, 6000));
  CHECK(stats.link_ratio_pct.load() > 0 && stats.link_ratio_pct.load() < 10);  // reported as far behind
  env.stop = true;
  session.join();
}

static void test_fast_link_reads_full() {
  FakeServer server([](int fd, int, FakeServer& self) {
    const auto g = greeting(5, 29);
    FakeServer::write_all(fd, g.data(), g.size());
    stream_pattern(fd, static_cast<size_t>(1) << 40, self);  // until the test stops it
  });
  TestEnv env;
  Stats stats;
  const auto cfg = fast_config(server.port());
  std::thread session([&] { run(cfg, env, stats); });
  CHECK(wait_for([&] { return stats.link_ratio_pct.load() > 0; }, 6000));
  CHECK(stats.link_ratio_pct.load() >= 100);  // loopback easily exceeds 4.8 MB/s
  env.stop = true;
  session.join();
}

static void test_stop_is_prompt() {
  FakeServer server([](int fd, int, FakeServer& self) {
    const auto g = greeting(5, 29);
    FakeServer::write_all(fd, g.data(), g.size());
    stream_pattern(fd, 100000000, self);
  });
  TestEnv env;
  Stats stats;
  const auto cfg = fast_config(server.port());
  std::thread session([&] { run(cfg, env, stats); });
  CHECK(wait_for([&] { return env.delivered_total() > 10000; }, 3000));
  const auto asked = std::chrono::steady_clock::now();
  env.stop = true;
  session.join();
  CHECK(std::chrono::steady_clock::now() - asked < std::chrono::milliseconds(1000));

  FakeServer silent([](int, int, FakeServer&) { FakeServer::sleep(2000); });
  TestEnv env2;
  Stats stats2;
  auto cfg2 = fast_config(silent.port());
  cfg2.greeting_timeout_ms = 60000;  // stop must beat the timeout
  std::thread session2([&] { run(cfg2, env2, stats2); });
  FakeServer::sleep(200);
  const auto asked2 = std::chrono::steady_clock::now();
  env2.stop = true;
  session2.join();
  CHECK(std::chrono::steady_clock::now() - asked2 < std::chrono::milliseconds(1000));
}

int main() {
  test_normal_stream_and_commands();
  test_wrong_service_is_refused();
  test_silent_and_truncated_servers();
  test_nothing_listening();
  test_keeps_retrying_and_recovers();
  test_greeting_without_samples_keeps_backing_off();
  test_reconnect_has_no_stale_samples();
  test_full_ring_drops_are_counted_and_aligned();
  test_short_dsp_stall_loses_nothing();
  test_slow_link_is_reported();
  test_fast_link_reads_full();
  test_stop_is_prompt();
  std::printf("RTL_TCP_SESSION_HOST_TEST failures=%d pass=%d\n", g_failures, g_failures == 0);
  return g_failures == 0 ? 0 : 1;
}
