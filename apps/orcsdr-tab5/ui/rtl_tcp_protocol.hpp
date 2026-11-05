#pragma once

// rtl_tcp wire protocol, client side. Pure C++: no sockets, no ESP-IDF, no
// allocation, so every rule here is testable on a host without hardware.
// Written from the wire format only (12-byte "RTL0" greeting, 5-byte
// big-endian commands, raw unsigned 8-bit I/Q); no upstream code is copied.

#include <cstddef>
#include <cstdint>

namespace orcsdr::rtl_tcp {

constexpr size_t kGreetingBytes = 12;
constexpr size_t kCommandBytes = 5;
constexpr uint16_t kDefaultPort = 1234;
// A real RTL tuner reports a few dozen gain steps. A larger count means the
// peer is not speaking rtl_tcp, so the greeting is refused.
constexpr uint32_t kMaxPlausibleGainCount = 256;

enum class Command : uint8_t {
  set_frequency = 0x01,
  set_sample_rate = 0x02,
  set_gain_mode = 0x03,
  set_gain = 0x04,
  set_freq_correction = 0x05,
  set_if_gain = 0x06,
  set_test_mode = 0x07,
  set_agc_mode = 0x08,
  set_direct_sampling = 0x09,
  set_offset_tuning = 0x0a,
  set_rtl_xtal = 0x0b,
  set_tuner_xtal = 0x0c,
  set_gain_by_index = 0x0d,
  set_bias_tee = 0x0e,
};

// The server never acknowledges a command, so success is never reported.
void encode_command(Command command, uint32_t parameter, uint8_t out[kCommandBytes]);

// Parameter helpers: the wire carries unsigned 32-bit words.
uint32_t parameter_signed(int32_t value);
uint32_t parameter_if_gain(uint16_t stage, int16_t gain_tenths_db);

// Tuner type is kept raw: servers other than librtlsdr may report other values.
struct Greeting {
  uint32_t tuner_type_raw = 0;
  uint32_t gain_count = 0;
};
const char* tuner_type_name(uint32_t tuner_type_raw);

enum class GreetingStatus : uint8_t { need_more, ok, bad_magic, bad_field };

// Feeds stream bytes into the greeting. `consumed` reports how many bytes the
// greeting took; once status is `ok`, the rest of the buffer is IQ and must be
// handed on untouched. A wrong magic byte is refused at once, without waiting
// for twelve bytes, so a non-rtl_tcp service is rejected quickly.
class GreetingParser {
 public:
  void reset();
  GreetingStatus feed(const uint8_t* data, size_t length, size_t* consumed);
  GreetingStatus status() const { return status_; }
  const Greeting& greeting() const { return greeting_; }

 private:
  uint8_t raw_[kGreetingBytes]{};
  size_t have_ = 0;
  GreetingStatus status_ = GreetingStatus::need_more;
  Greeting greeting_{};
};

// IQ arrives as an arbitrary TCP byte stream. An I/Q pair is two bytes, so a
// read may end between I and Q. This carries a trailing odd byte into the next
// buffer so that I stays first and Q second and no sample is ever shifted.
//   offset = carry.prepare(slot);          // slot[0] holds a carried byte or not
//   n = recv(slot + offset, capacity - offset);
//   usable = carry.commit(slot, offset, n); // even byte count to hand to DSP
// A byte stays carried until commit(), so a read that times out and is retried
// or released never loses it.
class IqPairCarry {
 public:
  size_t prepare(uint8_t* slot);
  size_t commit(uint8_t* slot, size_t offset, size_t received);
  // Reconnect or source switch: a carried byte belongs to the old stream.
  void reset() { has_ = false; }
  bool pending() const { return has_; }

 private:
  bool has_ = false;
  uint8_t byte_ = 0;
};

// A server is named by an IPv4 address or a host name; nothing else is accepted
// (no spaces, no URL parts, no IPv6 literals in this first version).
constexpr size_t kMaxHostChars = 63;
bool valid_host(const char* host);

// Bounded exponential reconnect delay.
struct BackoffPolicy {
  uint32_t initial_ms = 500;
  uint32_t max_ms = 15000;
  uint32_t max_attempts = 8;
};
// `attempt` counts from 0. Returns false once the attempts are spent.
bool reconnect_delay_ms(const BackoffPolicy& policy, uint32_t attempt, uint32_t* delay_ms);

}  // namespace orcsdr::rtl_tcp
