#pragma once
// esp-rtl-tcp server half (docs/hardware/ESP_RTL_TCP_DESIGN.md, revision 2). One client at a time; a second connection waits in the
// listen backlog. The header goes out once, after that only raw CU8 samples; counters and the reason a session ended are reported
// through stats(), never on the wire. The host application implements Host and feeds blocks through push().
#include <cstddef>
#include <cstdint>

namespace esp_rtl_tcp {

enum class RateResult : uint8_t { ok, unsupported, exceeds_link, failed };

enum class EndReason : uint8_t {
  none,
  client_closed,
  socket_error,
  slow_peer,          // a send did not complete within Config::send_timeout_ms
  receiver_changed,   // listen-only: another owner changed the centre frequency or sample rate
  rate_unsupported,   // control mode: the requested rate cannot be run
  rate_exceeds_link,  // control mode: the requested rate is more than the link can carry
  rate_change_failed,
  host_refused,       // Host::session_begin() said no (no receiver, no memory)
  stopped,            // the server was stopped
};
const char* end_reason_name(EndReason reason);

// Implemented by the application. Every method runs in the server's task unless noted.
class Host {
 public:
  virtual ~Host() = default;
  // rtl_tcp's tuner enum value (see rtl_tcp_proto.hpp) and the length of the gain ladder, both for the connecting client. Held for the whole session.
  virtual uint32_t tuner_type() = 0;
  virtual uint32_t gain_count() = 0;
  // After the header is sent: start delivering blocks with push() (typically add a queued sink). False ends the session (host_refused).
  virtual bool session_begin() = 0;
  // After the socket is shut: stop delivering. Must not return while push() can still be running.
  virtual void session_end() = 0;
  // Control mode only. Defaults refuse, so a host that never enables control implements nothing here.
  virtual bool set_center(uint32_t) { return false; }
  virtual RateResult set_rate(uint32_t) { return RateResult::unsupported; }
  virtual bool set_gain_mode(bool /*manual*/) { return false; }
  virtual bool set_gain_tenth_db(int) { return false; }
  virtual bool set_ppm(int) { return false; }
  virtual bool set_rtl_agc(bool) { return false; }
  virtual bool set_gain_index(uint32_t) { return false; }
  virtual bool set_bias_tee(bool) { return false; }
};

struct Config {
  uint16_t port = 1234;
  bool allow_control = false;       // false = listen-only: tuning commands are counted and ignored
  uint32_t allow_ip = 0;            // network-order IPv4 of the only accepted client; 0 = any
  uint32_t send_timeout_ms = 2000;  // provisional (design decision 9): a send blocked this long ends the session as slow_peer
  int core = 0;
  uint32_t task_stack = 6144;
};

struct Stats {
  bool running = false;
  bool client_connected = false;
  bool control_allowed = false;
  uint32_t clients_total = 0, clients_rejected = 0;
  uint64_t bytes_sent = 0;
  uint32_t blocks_sent = 0;
  uint32_t commands_applied = 0, commands_rejected = 0, commands_ignored = 0, commands_unsupported = 0, commands_unknown = 0;
  EndReason last_end = EndReason::none;
  char peer[16] = {};
  uint32_t session_rate = 0, session_center_hz = 0;  // what the current session's samples are
};

bool start(const Config& config, Host& host);  // false if already running or the socket could not be opened
void stop();                                   // ends any session and waits for the task
bool running();
Stats stats();

// Called by the host for every block (from its queued-sink task). Sends the bytes to the client, blocking up to send_timeout_ms; ends the session on
// failure. `rate` and `center_hz` describe this block: the first block of a session defines what the client was told; in listen-only mode a later
// block with another rate or centre ends the session (receiver_changed). Does nothing when no client is streaming.
void push(const uint8_t* data, size_t bytes, uint32_t rate, uint32_t center_hz);

}  // namespace esp_rtl_tcp
