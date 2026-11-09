#include "rtl_tcp_session.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace orcsdr::rtl_tcp {
namespace {

constexpr size_t kMaxChunk = 32768;       // same block size the USB path delivers
constexpr uint32_t kRecvPollMs = 200;     // bounds stop latency and stall checks
constexpr uint32_t kSendTimeoutMs = 1000;
constexpr uint32_t kHealthyStreamMs = 2000;
constexpr uint64_t kHealthyStreamBytes = 65536;  // about two read chunks; a real stream moves far more in two seconds
constexpr size_t kScratchBytes = 2048;
constexpr uint32_t kRingWaitMs = 40;      // about two 960 kS/s blocks of DSP time

bool timed_out(int error) {
  return error == EAGAIN || error == EWOULDBLOCK || error == ETIMEDOUT;
}

void set_timeout(int fd, int option, uint32_t ms) {
  timeval tv{static_cast<time_t>(ms / 1000), static_cast<suseconds_t>((ms % 1000) * 1000)};
  setsockopt(fd, SOL_SOCKET, option, &tv, sizeof(tv));
}

// Non-blocking connect that still notices a stop request.
int connect_to(const SessionConfig& config, Env& env, int* error) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port[8];
  std::snprintf(port, sizeof(port), "%u", static_cast<unsigned>(config.port));
  addrinfo* found = nullptr;
  if (getaddrinfo(config.host, port, &hints, &found) != 0 || found == nullptr) {
    *error = EHOSTUNREACH;
    return -1;
  }
  const int fd = socket(found->ai_family, found->ai_socktype, 0);
  if (fd < 0) {
    *error = errno;
    freeaddrinfo(found);
    return -1;
  }
  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int rc = connect(fd, found->ai_addr, found->ai_addrlen);
  freeaddrinfo(found);
  if (rc < 0 && errno != EINPROGRESS) {
    *error = errno;
    close(fd);
    return -1;
  }
  const uint32_t started = env.now_ms();
  while (rc < 0) {
    if (env.stop_requested()) {
      *error = ECANCELED;
      close(fd);
      return -1;
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval wait{0, 100 * 1000};
    const int ready = select(fd + 1, nullptr, &writable, nullptr, &wait);
    if (ready > 0) {
      int so_error = 0;
      socklen_t length = sizeof(so_error);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &length);
      if (so_error != 0) {
        *error = so_error;
        close(fd);
        return -1;
      }
      break;
    }
    if (env.now_ms() - started >= config.connect_timeout_ms) {
      *error = ETIMEDOUT;
      close(fd);
      return -1;
    }
  }
  fcntl(fd, F_SETFL, flags);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  set_timeout(fd, SO_RCVTIMEO, kRecvPollMs);
  set_timeout(fd, SO_SNDTIMEO, kSendTimeoutMs);
  return fd;
}

bool send_all(int fd, const uint8_t* data, size_t length, Env& env) {
  size_t sent = 0;
  while (sent < length) {
    if (env.stop_requested()) return false;
    const ssize_t n = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
    if (n > 0) {
      sent += static_cast<size_t>(n);
    } else if (n < 0 && timed_out(errno)) {
      return false;  // a stuck send is a dead connection, not something to wait on
    } else {
      return false;
    }
  }
  return true;
}

bool send_command(int fd, Command command, uint32_t parameter, Env& env) {
  uint8_t bytes[kCommandBytes];
  encode_command(command, parameter, bytes);
  return send_all(fd, bytes, sizeof(bytes), env);
}

bool send_desired(int fd, const Desired& d, Env& env) {
  if (d.sample_rate_sps && !send_command(fd, Command::set_sample_rate, d.sample_rate_sps, env))
    return false;
  if (d.frequency_hz && !send_command(fd, Command::set_frequency, d.frequency_hz, env))
    return false;
  if (d.gain_mode_set &&
      !send_command(fd, Command::set_gain_mode, d.gain_manual ? 1u : 0u, env))
    return false;
  if (d.gain_set &&
      !send_command(fd, Command::set_gain, parameter_signed(d.gain_tenths_db), env))
    return false;
  if (d.agc_set && !send_command(fd, Command::set_agc_mode, d.agc_on ? 1u : 0u, env))
    return false;
  if (d.ppm_set && !send_command(fd, Command::set_freq_correction, parameter_signed(d.ppm), env))
    return false;
  if (d.offset_set &&
      !send_command(fd, Command::set_offset_tuning, d.offset_on ? 1u : 0u, env))
    return false;
  if (d.direct_set && !send_command(fd, Command::set_direct_sampling, d.direct_mode, env))
    return false;
  return true;
}

}  // namespace

const char* state_name(State state) {
  switch (state) {
    case State::connecting: return "connecting";
    case State::streaming: return "streaming";
    case State::reconnecting: return "reconnecting";
    case State::failed: return "failed";
    default: return "idle";
  }
}

const char* end_reason_name(EndReason reason) {
  switch (reason) {
    case EndReason::stopped: return "stopped";
    case EndReason::connect_failed: return "connect failed";
    case EndReason::greeting_failed: return "not an rtl_tcp server";
    case EndReason::closed_by_peer: return "closed by server";
    case EndReason::stalled: return "no data";
    case EndReason::socket_error: return "socket error";
    case EndReason::no_greeting: return "server busy or silent";
    default: return "none";
  }
}

EndReason run_connection(const SessionConfig& config, Env& env, Stats& stats) {
  env.on_state(State::connecting, EndReason::none);
  int error = 0;
  const int fd = connect_to(config, env, &error);
  if (fd < 0) {
    stats.last_errno.store(error, std::memory_order_relaxed);
    return env.stop_requested() ? EndReason::stopped : EndReason::connect_failed;
  }

  GreetingParser parser;
  uint8_t lead[64];
  size_t lead_length = 0;
  size_t greeting_bytes = 0;
  const uint32_t greeting_started = env.now_ms();
  while (parser.status() == GreetingStatus::need_more) {
    if (env.stop_requested()) {
      close(fd);
      return EndReason::stopped;
    }
    if (env.now_ms() - greeting_started >= config.greeting_timeout_ms) {
      close(fd);
      // Nothing at all is not the same as something wrong: a second client of a one-client rtl_tcp waits in silence, and so does a wedged dongle.
      return greeting_bytes == 0 ? EndReason::no_greeting : EndReason::greeting_failed;
    }
    uint8_t chunk[sizeof(lead)];
    const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
    if (n > 0) {
      greeting_bytes += static_cast<size_t>(n);
      size_t used = 0;
      parser.feed(chunk, static_cast<size_t>(n), &used);
      if (parser.status() == GreetingStatus::ok) {
        lead_length = static_cast<size_t>(n) - used;
        std::memcpy(lead, chunk + used, lead_length);
      }
    } else if (n == 0) {
      close(fd);
      return EndReason::greeting_failed;  // dropped before the greeting finished
    } else if (!timed_out(errno)) {
      stats.last_errno.store(errno, std::memory_order_relaxed);
      close(fd);
      return EndReason::socket_error;
    }
  }
  if (parser.status() != GreetingStatus::ok) {
    close(fd);
    return EndReason::greeting_failed;
  }
  stats.tuner_type_raw.store(parser.greeting().tuner_type_raw, std::memory_order_relaxed);
  stats.gain_count.store(parser.greeting().gain_count, std::memory_order_relaxed);

  if (!send_desired(fd, env.desired(), env)) {
    close(fd);
    return env.stop_requested() ? EndReason::stopped : EndReason::socket_error;
  }
  if (stats.connections.fetch_add(1, std::memory_order_relaxed) > 0) env.discontinuity();
  env.on_state(State::streaming, EndReason::none);

  IqPairCarry carry;
  const uint32_t stream_started = env.now_ms();
  const uint64_t bytes_at_stream_start = stats.bytes_received.load(std::memory_order_relaxed);
  uint32_t last_rx = stream_started;
  EndReason reason = EndReason::none;

  // Bytes that arrived in the same read as the greeting are already IQ.
  if (lead_length > 0) {
    uint8_t* slot = nullptr;
    size_t capacity = 0;
    if (env.acquire(&slot, &capacity) && capacity >= lead_length) {
      std::memcpy(slot, lead, lead_length);
      const size_t usable = carry.commit(slot, 0, lead_length);
      stats.bytes_received.fetch_add(lead_length, std::memory_order_relaxed);
      if (usable > 0) {
        env.submit(slot, usable);
        stats.bytes_delivered.fetch_add(usable, std::memory_order_relaxed);
      } else {
        env.release(slot);
      }
    } else {
      if (slot != nullptr) env.release(slot);
      uint8_t scratch[sizeof(lead)];
      std::memcpy(scratch, lead, lead_length);
      stats.bytes_dropped.fetch_add(carry.commit(scratch, 0, lead_length),
                                    std::memory_order_relaxed);
    }
  }

  stats.link_ratio_pct.store(0, std::memory_order_relaxed);  // never show a stale figure
  uint32_t window_started = stream_started;
  uint64_t window_base = stats.bytes_received.load(std::memory_order_relaxed);
  uint8_t scratch[kScratchBytes];
  while (reason == EndReason::none) {
    const uint32_t window_now = env.now_ms();
    if (window_now - window_started >= kLinkWindowMs) {
      const uint64_t received = stats.bytes_received.load(std::memory_order_relaxed);
      const uint64_t expected_per_s = static_cast<uint64_t>(env.desired().sample_rate_sps) * 2;
      const uint64_t measured_per_s = (received - window_base) * 1000 / (window_now - window_started);
      if (expected_per_s > 0) {
        const uint64_t ratio = measured_per_s * 100 / expected_per_s;
        stats.link_ratio_pct.store(ratio > 999 ? 999u : static_cast<uint32_t>(ratio),
                                   std::memory_order_relaxed);
      }
      window_started = window_now;
      window_base = received;
    }
    if (env.stop_requested()) {
      reason = EndReason::stopped;
      break;
    }
    uint8_t command[kCommandBytes];
    while (env.pop_command(command)) {
      if (!send_all(fd, command, sizeof(command), env)) {
        reason = env.stop_requested() ? EndReason::stopped : EndReason::socket_error;
        break;
      }
    }
    if (reason != EndReason::none) break;

    uint8_t* slot = nullptr;
    size_t capacity = 0;
    bool have_slot = env.acquire(&slot, &capacity);
    // TCP already pushes back on the server while we wait, so a short wait for the
    // DSP to free a slot loses nothing. Only a DSP stalled longer than this drops.
    for (uint32_t waited = 0; !have_slot && waited < kRingWaitMs && !env.stop_requested(); ++waited) {
      env.sleep_ms(1);
      have_slot = env.acquire(&slot, &capacity);
    }
    uint8_t* target = have_slot ? slot : scratch;
    size_t room = have_slot ? (capacity < kMaxChunk ? capacity : kMaxChunk) : sizeof(scratch);
    const size_t offset = carry.prepare(target);
    const ssize_t n = recv(fd, target + offset, room - offset, 0);
    if (n > 0) {
      const uint32_t now = env.now_ms();
      last_rx = now;
      stats.bytes_received.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
      const size_t usable = carry.commit(target, offset, static_cast<size_t>(n));
      if (have_slot && usable > 0) {
        env.submit(slot, usable);
        stats.bytes_delivered.fetch_add(usable, std::memory_order_relaxed);
      } else {
        if (have_slot) env.release(slot);
        // No free slot: the bytes were read so latency stays bounded, and the
        // loss is counted instead of hidden.
        if (!have_slot) stats.bytes_dropped.fetch_add(usable, std::memory_order_relaxed);
      }
    } else {
      if (have_slot) env.release(slot);
      if (n == 0) {
        reason = EndReason::closed_by_peer;
      } else if (timed_out(errno)) {
        if (env.now_ms() - last_rx >= config.stall_timeout_ms) reason = EndReason::stalled;
      } else {
        stats.last_errno.store(errno, std::memory_order_relaxed);
        reason = EndReason::socket_error;
      }
    }
  }
  stats.last_stream_ms.store(env.now_ms() - stream_started, std::memory_order_relaxed);
  stats.last_stream_bytes.store(stats.bytes_received.load(std::memory_order_relaxed) - bytes_at_stream_start,
                                std::memory_order_relaxed);
  close(fd);
  return reason;
}

State run(const SessionConfig& config, Env& env, Stats& stats) {
  uint32_t attempt = 0;
  while (!env.stop_requested()) {
    const EndReason reason = run_connection(config, env, stats);
    if (reason == EndReason::stopped) break;
    const uint32_t streamed_ms = stats.last_stream_ms.exchange(0, std::memory_order_relaxed);
    const uint64_t streamed_bytes = stats.last_stream_bytes.exchange(0, std::memory_order_relaxed);
    if (streamed_ms >= kHealthyStreamMs && streamed_bytes >= kHealthyStreamBytes)
      attempt = 0;  // it streamed samples for a while; the next failure starts a fresh back-off
    uint32_t delay = 0;
    if (!reconnect_delay_ms(config.backoff, attempt, &delay)) {
      env.on_state(State::failed, reason);
      return State::failed;
    }
    ++attempt;
    stats.reconnects.fetch_add(1, std::memory_order_relaxed);
    env.on_state(State::reconnecting, reason);
    for (uint32_t waited = 0; waited < delay && !env.stop_requested(); waited += 25)
      env.sleep_ms(25);
  }
  env.on_state(State::idle, EndReason::stopped);
  return State::idle;
}

}  // namespace orcsdr::rtl_tcp
