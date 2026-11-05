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
void stop();  // bounded wait, closes the socket
bool running();
rtl_tcp::State state();
bool streaming();

// Live controls. Each is sent to the server, which never acknowledges, so
// `true` means "queued", never "the hardware applied it".
bool retune(uint32_t frequency_hz);
bool set_gain_manual(bool manual);
bool set_gain_tenths_db(int32_t tenths_db);
bool set_agc(bool on);

Status status();
uint64_t bytes_total();

// Serial/console commands. Returns true when `line` was an RTL_TCP command and
// writes a one-line reply.
bool handle_command(const char* line, char* reply, size_t reply_size);

}  // namespace orcsdr::rtl_tcp_source
