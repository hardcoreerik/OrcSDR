#pragma once

// rtl_tcp server loop: accept one controlling client, send the greeting, stream
// IQ from the backend, and hand commands to it. Plain BSD sockets and no RTOS
// calls, so it runs in a host test with a fake backend and on the Tab5 with a
// backend that drives esp-rtl-sdr. The server never talks to the radio itself.
//
// Ownership policy: the first client to connect owns the receiver, tuning and
// all. Any other connection is closed at once and counted, never queued, so a
// second application is told "busy" rather than silently left hanging.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "rtl_tcp_protocol.hpp"

namespace orcsdr::rtl_tcp {

enum class ServerState : uint8_t { stopped, listening, serving, failed };
const char* server_state_name(ServerState state);

class ServerBackend {
 public:
  virtual ~ServerBackend() = default;
  // Tuner type and gain count to put in the greeting.
  virtual Greeting greeting() = 0;
  // A client was accepted. Start streaming. False = cannot serve now; the
  // client is closed before any greeting.
  virtual bool client_connected() = 0;
  virtual void client_disconnected() = 0;
  // Commands arrive unacknowledged and may be unknown ids; the server counts
  // unknown ones and does not pass them on.
  virtual void apply(const RawCommand& command) = 0;
  // Non-blocking. Returns an even number of bytes, 0 when none are ready.
  virtual size_t read_iq(uint8_t* destination, size_t max_bytes) = 0;
  // False when the receiver behind the stream is gone (for example the dongle was
  // unplugged); the client is then dropped instead of waiting on silence.
  virtual bool session_alive() { return true; }
  virtual bool stop_requested() = 0;
  virtual uint32_t now_ms() = 0;
  virtual void sleep_ms(uint32_t ms) = 0;
  virtual void on_state(ServerState) {}
};

struct ServerConfig {
  // Must be given explicitly: there is no default bind address, so the server
  // is never reachable from a network the owner did not name.
  char bind_host[kMaxHostChars + 1]{};
  uint16_t port = kDefaultPort;
  // A client that accepts no data for this long is dropped.
  uint32_t send_stall_ms = 5000;
};

struct ServerStats {
  std::atomic<uint64_t> bytes_sent{0};
  std::atomic<uint32_t> clients_accepted{0};
  std::atomic<uint32_t> clients_refused{0};  // second connection while one is served
  std::atomic<uint32_t> commands_applied{0};
  std::atomic<uint32_t> commands_unknown{0};
  std::atomic<uint32_t> stalled_clients{0};
  std::atomic<int> last_errno{0};
  std::atomic<uint32_t> select_ready{0};     // listen socket reported a waiting connection
  std::atomic<uint32_t> accept_failures{0};
};

// Runs until stop_requested(). Returns stopped, or failed if the socket could
// not be bound.
ServerState run_server(const ServerConfig& config, ServerBackend& backend, ServerStats& stats);

}  // namespace orcsdr::rtl_tcp
