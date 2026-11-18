// Replays a real IQ recording (raw unsigned 8-bit I/Q) through the rtl_tcp client
// over loopback and checks the network path changes nothing: the received bytes
// must equal the file, and a simple FM discriminator must give the same output on
// both copies. A host simulation of transport integrity, not a Tab5 measurement.
//
//   rtl_tcp_replay_host_test <recording.cu8> [chunk_seed]
// Without a file argument the test prints SKIP and succeeds, so CI needs no fixture.

#include "rtl_tcp_session.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

using namespace orcsdr::rtl_tcp;

class CollectEnv : public Env {
 public:
  bool acquire(uint8_t** slot, size_t* capacity) override {
    *slot = block_;
    *capacity = sizeof(block_);
    return true;
  }
  void submit(uint8_t* slot, size_t bytes) override { received.insert(received.end(), slot, slot + bytes); }
  void release(uint8_t*) override {}
  void discontinuity() override { ++discontinuities; }
  bool pop_command(uint8_t*) override { return false; }
  Desired desired() override {
    Desired d;
    d.sample_rate_sps = 960000;
    d.frequency_hz = 96100000;
    return d;
  }
  bool stop_requested() override { return stop.load(); }
  void on_state(State, EndReason) override {}
  uint32_t now_ms() override {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }
  void sleep_ms(uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
  std::atomic<bool> stop{false};
  std::vector<uint8_t> received;
  int discontinuities = 0;

 private:
  uint8_t block_[33280];
};

// Quadrature FM discriminator, decimated by 4 with a boxcar. Deterministic.
static std::vector<float> fm_demod(const uint8_t* iq, size_t bytes) {
  std::vector<float> out;
  float pi = 0, pq = 0, acc = 0;
  int count = 0;
  for (size_t n = 0; n + 1 < bytes; n += 2) {
    const float i = static_cast<float>(iq[n]) - 127.5f, q = static_cast<float>(iq[n + 1]) - 127.5f;
    acc += std::atan2(q * pi - i * pq, i * pi + q * pq);
    pi = i;
    pq = q;
    if (++count == 4) {
      out.push_back(acc * 0.25f);
      acc = 0;
      count = 0;
    }
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("RTL_TCP_REPLAY_HOST_TEST SKIP (no recording given)\n");
    return 0;
  }
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<uint8_t> recording((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  recording.resize(recording.size() & ~static_cast<size_t>(1));
  if (recording.size() < 100000) {
    std::printf("RTL_TCP_REPLAY_HOST_TEST FAIL recording too small or unreadable\n");
    return 1;
  }
  uint32_t seed = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 1;

  const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(listen_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  socklen_t length = sizeof(a);
  getsockname(listen_fd, reinterpret_cast<sockaddr*>(&a), &length);
  listen(listen_fd, 1);

  std::thread server([&] {
    const int fd = accept(listen_fd, nullptr, nullptr);
    uint8_t hello[kGreetingBytes];
    encode_greeting(5, 29, hello);
    send(fd, hello, sizeof(hello), MSG_NOSIGNAL);
    size_t sent = 0;
    uint32_t rng = seed;
    while (sent < recording.size()) {  // odd, irregular chunk sizes, like a real TCP path
      rng = rng * 1664525u + 1013904223u;
      size_t chunk = 1 + (rng >> 8) % 40000;
      if (chunk > recording.size() - sent) chunk = recording.size() - sent;
      size_t done = 0;
      while (done < chunk) {
        const ssize_t n = send(fd, recording.data() + sent + done, chunk - done, MSG_NOSIGNAL);
        if (n <= 0) return;
        done += static_cast<size_t>(n);
      }
      sent += chunk;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the client drain
    close(fd);
  });

  SessionConfig config;
  std::strcpy(config.host, "127.0.0.1");
  config.port = ntohs(a.sin_port);
  config.backoff = BackoffPolicy{10, 20, 1};
  CollectEnv env;
  Stats stats;
  std::thread client([&] { run(config, env, stats); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (env.received.size() < recording.size() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  env.stop = true;
  client.join();
  server.join();
  close(listen_fd);

  const bool identical = env.received.size() == recording.size() &&
                         std::memcmp(env.received.data(), recording.data(), recording.size()) == 0;
  const auto reference = fm_demod(recording.data(), recording.size());
  const auto networked = fm_demod(env.received.data(), env.received.size());
  const bool demod_equal = reference.size() == networked.size() &&
                           std::memcmp(reference.data(), networked.data(), reference.size() * sizeof(float)) == 0;
  double sum = 0, sum2 = 0;
  for (float v : networked) { sum += v; sum2 += static_cast<double>(v) * v; }
  const double mean = networked.empty() ? 0 : sum / networked.size();
  const double sigma = networked.empty() ? 0 : std::sqrt(sum2 / networked.size() - mean * mean);
  std::printf("replay: bytes=%zu received=%zu identical=%d fm_samples=%zu fm_equal=%d fm_sigma_rad=%.4f "
              "discontinuities=%d dropped=%llu\n",
              recording.size(), env.received.size(), identical ? 1 : 0, networked.size(), demod_equal ? 1 : 0,
              sigma, env.discontinuities, static_cast<unsigned long long>(stats.bytes_dropped.load()));
  const bool pass = identical && demod_equal && env.discontinuities == 0 && stats.bytes_dropped.load() == 0;
  std::printf("RTL_TCP_REPLAY_HOST_TEST pass=%d\n", pass ? 1 : 0);
  return pass ? 0 : 1;
}
