#pragma once

// rtl_tcp client connection loop: connect, validate the greeting, stream IQ,
// forward commands, reconnect with a bounded back-off. Plain BSD sockets and no
// RTOS calls, so the same code runs in a host test against a fake server and on
// the Tab5 (lwip). Everything platform specific arrives through Env.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "rtl_tcp_protocol.hpp"

namespace orcsdr::rtl_tcp {

enum class State : uint8_t { idle, connecting, streaming, reconnecting, failed };

enum class EndReason : uint8_t {
  none,
  stopped,
  connect_failed,
  greeting_failed,  // refused, wrong magic, implausible field, timeout or early close
  closed_by_peer,
  stalled,          // connected but no IQ for stall_timeout_ms
  socket_error,
};

const char* state_name(State state);
const char* end_reason_name(EndReason reason);

struct SessionConfig {
  char host[kMaxHostChars + 1]{};
  uint16_t port = kDefaultPort;
  uint32_t connect_timeout_ms = 5000;
  uint32_t greeting_timeout_ms = 3000;
  uint32_t stall_timeout_ms = 3000;
  BackoffPolicy backoff{};
};

// Receiver settings to re-apply after every (re)connect. A set_* flag false
// means "leave the server's own value alone".
struct Desired {
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  bool gain_mode_set = false;
  bool gain_manual = false;
  bool gain_set = false;
  int32_t gain_tenths_db = 0;
  bool agc_set = false;
  bool agc_on = false;
  bool ppm_set = false;
  int32_t ppm = 0;
  bool offset_set = false;
  bool offset_on = false;
};

struct Stats {
  std::atomic<uint64_t> bytes_received{0};
  std::atomic<uint64_t> bytes_delivered{0};
  std::atomic<uint64_t> bytes_dropped{0};  // read and discarded because the DSP ring was full
  std::atomic<uint32_t> connections{0};
  std::atomic<uint32_t> reconnects{0};
  std::atomic<uint32_t> tuner_type_raw{0};
  std::atomic<uint32_t> gain_count{0};
  std::atomic<int> last_errno{0};
  std::atomic<uint32_t> last_stream_ms{0};
  // Received bytes/s as a percentage of what the requested sample rate needs
  // (2 bytes per sample), measured over 2 s windows. Well under 100 means the
  // link cannot keep up: the server is queueing and what is shown falls behind
  // real time. 0 = not measured yet.
  std::atomic<uint32_t> link_ratio_pct{0};
};

constexpr uint32_t kLinkWindowMs = 2000;

class Env {
 public:
  virtual ~Env() = default;
  // IQ ring. acquire() must not block; false means the ring is full.
  virtual bool acquire(uint8_t** slot, size_t* capacity) = 0;
  virtual void submit(uint8_t* slot, size_t bytes) = 0;
  virtual void release(uint8_t* slot) = 0;
  // Samples before this point and after it are not contiguous (reconnect).
  virtual void discontinuity() = 0;
  virtual bool pop_command(uint8_t out[kCommandBytes]) = 0;
  virtual Desired desired() = 0;
  virtual bool stop_requested() = 0;
  virtual void on_state(State state, EndReason reason) = 0;
  virtual uint32_t now_ms() = 0;
  virtual void sleep_ms(uint32_t ms) = 0;
};

// One connection attempt through to its end.
EndReason run_connection(const SessionConfig& config, Env& env, Stats& stats);

// Connect, stream and reconnect until stopped or the back-off is spent.
// Returns the final state (idle after stop, failed when attempts ran out).
State run(const SessionConfig& config, Env& env, Stats& stats);

}  // namespace orcsdr::rtl_tcp
