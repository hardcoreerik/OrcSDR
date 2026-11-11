#pragma once

// Remote receiver source for OrcSDR: an rtl_tcp client running on its own task.
// It is optional. Nothing here touches the USB driver, and the USB path never
// calls into the network. The application supplies the IQ ring through IqSink.

#include <cstddef>
#include <cstdint>

#include "receiver_source_policy.hpp"
#include "rtl_tcp_session.hpp"

namespace orcsdr::rtl_tcp_source {

struct IqSink {
  bool (*acquire)(uint8_t** slot, size_t* capacity);  // non-blocking
  void (*submit)(uint8_t* slot, size_t bytes);
  void (*release)(uint8_t* slot);
  void (*discontinuity)();  // IQ before and after this call is not contiguous
};

struct Status {
  rtl_tcp::State state = rtl_tcp::State::idle;
  rtl_tcp::EndReason last_reason = rtl_tcp::EndReason::none;
  char host[rtl_tcp::kMaxHostChars + 1]{};
  uint16_t port = rtl_tcp::kDefaultPort;
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t tuner_type_raw = 0;  // 0 = not reported
  uint32_t gain_count = 0;
  uint64_t bytes_received = 0;
  uint64_t bytes_dropped = 0;
  uint32_t connections = 0;
  uint32_t reconnects = 0;
  int last_errno = 0;
  uint32_t link_ratio_pct = 0;  // received rate vs what the sample rate needs; 0 = unknown
};

// Percent of the needed byte rate that is actually arriving (0 = not measured).
uint32_t link_ratio_pct();

// Remote sample rate for a stream that wants `wanted_sps`. It starts at the wanted rate and
// steps down a ladder (1.44 -> 0.96 -> 0.24 MS/s) when the link cannot keep up, so audio
// keeps real time instead of stuttering, and probes back up after a healthy minute. The step is
// forgotten when the mode or server changes.
uint32_t adapted_rate(uint32_t wanted_sps);
// Call about every 2 s with the current link ratio while streaming. Returns down or up when the
// rate just changed and the caller should restart the stream at adapted_rate().
rtl_tcp::RateAdapter::Change note_link_and_adapt(uint32_t link_pct, uint32_t wanted_sps, uint32_t floor_sps);

// Loads the saved mode and server and remembers the IQ sink. Does not touch the network.
void begin(const IqSink& sink);

source::Mode mode();
bool set_mode(source::Mode mode);  // persisted
bool configured();
bool set_endpoint(const char* host, uint16_t port);  // validated and persisted
const char* host();
uint16_t port();

// Starts the worker; returns at once. Poll state() for streaming / failed.
bool start(uint32_t sample_rate_sps, uint32_t frequency_hz);
// start() then wait, bounded, for the first working connection. False on timeout,
// failure or cancel; the worker is stopped again in that case. While the server accepts but
// stays silent (its one slot is held by another client) the wait stretches to two minutes so
// the start succeeds as soon as the slot frees; a cancel still ends it at once.
bool start_and_wait(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms,
                    bool (*cancelled)());
// The worker is running and has not given up (reconnecting still counts as alive).
bool alive();
void stop();  // bounded wait, closes the socket
bool running();
rtl_tcp::State state();
bool streaming();

// Live controls. Each is sent to the server, which never acknowledges, so
// `true` means "queued", never "the hardware applied it".
bool retune(uint32_t frequency_hz);
// Puts the desired gain state back to tuner AGC on, digital AGC off. rtl_tcp keeps tuner state across clients, so a stream for
// a different band must start from this instead of inheriting the last band's gain. Applied at the next connect.
void reset_gain_to_auto();
bool set_gain_manual(bool manual);
bool set_gain_tenths_db(int32_t tenths_db);
bool set_agc(bool on);

// What the UI last asked of the server (the protocol has no readback, so this is the only truth there is) and what the greeting said about the tuner.
// `*_known` is false until the user has changed that setting: the server then keeps its own default (tuner AGC, gain 0).
struct RemoteGain {
  bool mode_known = false;
  bool manual = false;
  bool gain_known = false;
  int32_t gain_tenths_db = 0;
  bool agc_known = false;
  bool agc_on = false;
  uint32_t tuner_type_raw = 0;
  uint32_t gain_count = 0;
};
RemoteGain remote_gain();

// User settings for the remote receiver (the Settings > RTL_TCP page). All are saved. The rate cap and the offset/direct/PPM values are
// sent to the server on every (re)connect once the user has set them; until then the server keeps its own defaults.
// Rate cap: 0 = adapt from the dashboard's own rate (up to 1.44 MS/s), 1 = never above 0.96 MS/s, 2 = 0.24 MS/s only.
struct Tuning {
  uint8_t rate_cap = 0;
  bool ppm_set = false;
  int32_t ppm = 0;
  bool direct_set = false;
  uint8_t direct_mode = 0;   // 0 off, 1 I branch, 2 Q branch
  bool offset_set = false;
  bool offset_on = false;
};
Tuning tuning();
bool set_rate_cap(uint8_t cap);              // takes effect on the next start
bool set_ppm(int32_t ppm);                   // -1000..1000; live when streaming
bool set_direct_sampling(uint8_t mode);      // 0..2; live when streaming
bool set_offset_tuning(bool on);             // live when streaming
// On by default. While a remote receiver is selected (source remote, or automatic with no dongle plugged in), main.cpp re-arms Wi-Fi
// reconnect and C6 link recovery every 5 minutes while Wi-Fi stays down, instead of giving up after its bounded attempts. Saved.
bool wifi_keepalive();
void set_wifi_keepalive(bool on);
const char* rate_cap_name(uint8_t cap);

Status status();
uint64_t bytes_total();

// Serial/console commands. Returns true when `line` was an RTL_TCP command and
// writes a one-line reply.
bool handle_command(const char* line, char* reply, size_t reply_size);
// Deletes a finished bench task. Call from normal application code; cheap when idle.
void reap();

}  // namespace orcsdr::rtl_tcp_source
