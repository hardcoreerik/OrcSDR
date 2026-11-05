// Host test of the rtl_tcp server loop with a fake backend and raw-socket clients.
// A simulation of the protocol and the ownership policy, not a Tab5 measurement.

#include "rtl_tcp_server_session.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

class FakeBackend : public ServerBackend {
 public:
  Greeting greeting() override { return Greeting{5, 29}; }
  bool client_connected() override {
    if (busy.load()) return false;
    index_ = 0;  // a new client always starts from fresh samples
    streaming_ = true;
    ++connects;
    return true;
  }
  void client_disconnected() override {
    streaming_ = false;
    ++disconnects;
  }
  void apply(const RawCommand& command) override {
    std::lock_guard<std::mutex> lock(mutex_);
    applied_.push_back(command);
  }
  size_t read_iq(uint8_t* destination, size_t max_bytes) override {
    if (!streaming_) return 0;
    const size_t n = (max_bytes < 4096 ? max_bytes : 4096) & ~static_cast<size_t>(1);
    for (size_t i = 0; i < n; ++i) destination[i] = pattern(index_ + i);
    index_ += n;
    return n;
  }
  bool session_alive() override { return alive.load(); }
  bool stop_requested() override { return stop.load(); }
  uint32_t now_ms() override {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }
  void sleep_ms(uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
  std::atomic<bool> alive{true};
  std::vector<RawCommand> applied() {
    std::lock_guard<std::mutex> lock(mutex_);
    return applied_;
  }
  std::atomic<bool> stop{false};
  std::atomic<bool> busy{false};
  std::atomic<int> connects{0};
  std::atomic<int> disconnects{0};

 private:
  uint64_t index_ = 0;
  bool streaming_ = false;
  std::mutex mutex_;
  std::vector<RawCommand> applied_;
};

static uint16_t free_port() {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  socklen_t length = sizeof(a);
  getsockname(fd, reinterpret_cast<sockaddr*>(&a), &length);
  const uint16_t port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static int client_connect(uint16_t port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(port);
  for (int attempt = 0; attempt < 100; ++attempt) {  // the server thread may not be listening yet
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) return fd;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  close(fd);
  return -1;
}

static bool read_exact(int fd, uint8_t* out, size_t length, int timeout_ms) {
  size_t got = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (got < length && std::chrono::steady_clock::now() < end) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    timeval wait{0, 50 * 1000};
    if (select(fd + 1, &readable, nullptr, nullptr, &wait) <= 0) continue;
    const ssize_t n = recv(fd, out + got, length - got, 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return got == length;
}

// True when the peer closed the connection without sending anything useful.
static bool closed_by_peer(int fd, int timeout_ms) {
  uint8_t byte;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < end) {
    const ssize_t n = recv(fd, &byte, 1, MSG_DONTWAIT);
    if (n == 0) return true;
    if (n < 0 && errno != EAGAIN) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

struct Harness {
  FakeBackend backend;
  ServerStats stats;
  ServerConfig config;
  std::thread thread;
  ServerState result = ServerState::stopped;
  uint16_t port;
  explicit Harness(uint32_t stall_ms = 5000) : port(free_port()) {
    std::strcpy(config.bind_host, "127.0.0.1");
    config.port = port;
    config.send_stall_ms = stall_ms;
    thread = std::thread([this] { result = run_server(config, backend, stats); });
  }
  void finish() {
    backend.stop = true;
    if (thread.joinable()) thread.join();
  }
  ~Harness() { finish(); }
};

static void test_greeting_stream_and_commands() {
  Harness h;
  const int fd = client_connect(h.port);
  CHECK(fd >= 0);
  uint8_t hello[12];
  CHECK(read_exact(fd, hello, sizeof(hello), 3000));
  const uint8_t expected[12] = {'R', 'T', 'L', '0', 0, 0, 0, 5, 0, 0, 0, 29};
  CHECK(std::memcmp(hello, expected, 12) == 0);

  // Commands arrive split across TCP writes and must still frame correctly.
  const uint8_t freq[5] = {0x01, 0x05, 0xF7, 0x67, 0xA0};
  send(fd, freq, 2, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  send(fd, freq + 2, 1, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  send(fd, freq + 3, 2, 0);
  const uint8_t unknown[5] = {0x20, 0, 0, 0, 1};  // not a defined command
  const uint8_t gain[5] = {0x04, 0, 0, 0, 0xC8};  // 20.0 dB
  send(fd, unknown, 5, 0);
  send(fd, gain, 5, 0);

  std::vector<uint8_t> iq(100000);
  CHECK(read_exact(fd, iq.data(), iq.size(), 5000));
  bool ordered = true;
  for (size_t i = 0; i < iq.size(); ++i)
    if (iq[i] != pattern(i)) ordered = false;
  CHECK(ordered);

  CHECK(wait_for([&] { return h.backend.applied().size() == 2; }, 3000));
  const auto applied = h.backend.applied();
  if (applied.size() == 2) {
    CHECK(applied[0].id == 0x01 && applied[0].parameter == 100100000u);
    CHECK(applied[1].id == 0x04 && applied[1].parameter == 200u);
  }
  CHECK(h.stats.commands_unknown.load() == 1);  // counted, not passed on, link stays up
  close(fd);
  CHECK(wait_for([&] { return h.backend.disconnects.load() == 1; }, 3000));
}

static void test_single_owner_and_fresh_restart() {
  Harness h;
  const int first = client_connect(h.port);
  uint8_t hello[12];
  CHECK(read_exact(first, hello, sizeof(hello), 3000));
  uint8_t chunk[64];
  CHECK(read_exact(first, chunk, sizeof(chunk), 3000));

  const int second = client_connect(h.port);  // a second application tries to join
  CHECK(second >= 0);
  CHECK(closed_by_peer(second, 3000));  // told "busy" by being closed, not left hanging
  CHECK(wait_for([&] { return h.stats.clients_refused.load() == 1; }, 3000));
  CHECK(read_exact(first, chunk, sizeof(chunk), 3000));  // the owner is not disturbed
  close(second);

  close(first);
  CHECK(wait_for([&] { return h.backend.disconnects.load() == 1; }, 3000));
  const int third = client_connect(h.port);
  CHECK(read_exact(third, hello, sizeof(hello), 3000));
  uint8_t head[64];
  CHECK(read_exact(third, head, sizeof(head), 3000));
  bool fresh = true;
  for (size_t i = 0; i < sizeof(head); ++i)
    if (head[i] != pattern(i)) fresh = false;
  CHECK(fresh);  // the new client starts at the first sample, nothing stale from the old one
  close(third);
}

static void test_stalled_client_is_dropped() {
  Harness h(400);
  const int silent = client_connect(h.port);
  uint8_t hello[12];
  CHECK(read_exact(silent, hello, sizeof(hello), 3000));
  // never read again: the socket buffers fill and the server must let go
  CHECK(wait_for([&] { return h.stats.stalled_clients.load() == 1; }, 15000));
  CHECK(wait_for([&] { return h.backend.disconnects.load() == 1; }, 3000));
  close(silent);
  const int next = client_connect(h.port);
  CHECK(read_exact(next, hello, sizeof(hello), 3000));
  close(next);
}

static void test_busy_backend_refuses() {
  Harness h;
  h.backend.busy = true;
  const int fd = client_connect(h.port);
  CHECK(fd >= 0);
  CHECK(closed_by_peer(fd, 3000));  // closed before any greeting
  CHECK(wait_for([&] { return h.stats.clients_refused.load() == 1; }, 3000));
  CHECK(h.backend.connects.load() == 0);
  close(fd);
}

static void test_receiver_loss_drops_client() {
  Harness h;
  const int fd = client_connect(h.port);
  uint8_t hello[12];
  CHECK(read_exact(fd, hello, sizeof(hello), 3000));
  h.backend.alive = false;  // for example the dongle was unplugged
  CHECK(wait_for([&] { return h.backend.disconnects.load() == 1; }, 3000));
  uint8_t sink[65536];
  bool closed = false;
  for (int i = 0; i < 200 && !closed; ++i) {  // drain what was in flight, then see the close
    const ssize_t n = recv(fd, sink, sizeof(sink), MSG_DONTWAIT);
    if (n == 0) closed = true;
    else if (n < 0 && errno != EAGAIN) closed = true;
    else std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(closed);
  close(fd);
}

static void test_bind_failures() {
  {
    FakeBackend backend;
    ServerStats stats;
    ServerConfig config;  // no bind address given
    config.port = free_port();
    CHECK(run_server(config, backend, stats) == ServerState::failed);
  }
  {
    FakeBackend backend;
    ServerStats stats;
    ServerConfig config;
    std::strcpy(config.bind_host, "not-an-address");
    config.port = free_port();
    CHECK(run_server(config, backend, stats) == ServerState::failed);
  }
  {
    const uint16_t port = free_port();  // occupy it first
    const int occupier = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    bind(occupier, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    listen(occupier, 1);
    FakeBackend backend;
    ServerStats stats;
    ServerConfig config;
    std::strcpy(config.bind_host, "127.0.0.1");
    config.port = port;
    CHECK(run_server(config, backend, stats) == ServerState::failed);
    CHECK(stats.last_errno.load() != 0);
    close(occupier);
  }
}

static void test_stop_is_prompt() {
  Harness h;
  const int fd = client_connect(h.port);
  uint8_t buf[256];
  CHECK(read_exact(fd, buf, sizeof(buf), 3000));
  const auto asked = std::chrono::steady_clock::now();
  h.backend.stop = true;
  h.thread.join();
  CHECK(std::chrono::steady_clock::now() - asked < std::chrono::milliseconds(1000));
  CHECK(h.result == ServerState::stopped);
  CHECK(h.backend.disconnects.load() == 1);  // the receiver is released on stop
  close(fd);
}

int main() {
  test_greeting_stream_and_commands();
  test_single_owner_and_fresh_restart();
  test_stalled_client_is_dropped();
  test_busy_backend_refuses();
  test_receiver_loss_drops_client();
  test_bind_failures();
  test_stop_is_prompt();
  std::printf("RTL_TCP_SERVER_SESSION_HOST_TEST failures=%d pass=%d\n", g_failures, g_failures == 0);
  return g_failures == 0 ? 0 : 1;
}
