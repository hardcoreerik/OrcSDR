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

// Server side: the greeting a client expects, and the command stream parser.
void encode_greeting(uint32_t tuner_type_raw, uint32_t gain_count, uint8_t out[kGreetingBytes]);

struct RawCommand {
  uint8_t id = 0;  // kept raw: ids above 0x0e are valid frames this server does not know
  uint32_t parameter = 0;
};

// Reassembles 5-byte commands from an arbitrary TCP byte stream.
class CommandParser {
 public:
  void reset() { have_ = 0; }
  // Returns true when `byte` completed a command.
  bool push(uint8_t byte, RawCommand* out);

 private:
  uint8_t raw_[kCommandBytes]{};
  size_t have_ = 0;
};

// A server is named by an IPv4 address or a host name; nothing else is accepted
// (no spaces, no URL parts, no IPv6 literals in this first version).
constexpr size_t kMaxHostChars = 63;
bool valid_host(const char* host);
// "host" or "host:port" (port 1-65535, default 1234) -> host and port. False if either
// part is invalid or the host does not fit `host_size`.
bool parse_endpoint(const char* text, char* host, size_t host_size, uint16_t* port);

// Bounded exponential reconnect delay.
struct BackoffPolicy {
  uint32_t initial_ms = 500;
  uint32_t max_ms = 15000;
  uint32_t max_attempts = 8;
};
// `attempt` counts from 0. Returns false once the attempts are spent.
bool reconnect_delay_ms(const BackoffPolicy& policy, uint32_t attempt, uint32_t* delay_ms);

// Steps the sample rate down by halves when the link stays behind real time, so a weak
// Wi-Fi link gives slower but live audio instead of fast-forwarded stutter. One call per
// link-ratio sample (about every 2 s). Pure, so it is host-tested.
// The rates are an explicit ladder because every rung must be a whole multiple of the 240 kS/s
// demodulation rate AND a rate the RTL2832 can run (225001-300000 and 900001-3200000 S/s only, so
// 480 kS/s is not possible on a real dongle). The adapter also climbs
// back: after kUpTicks healthy samples at a reduced rate it probes one rung up; a probe that
// falls behind again steps back down and doubles the wait (up to kUpTicksMax), so a link that
// cannot carry the higher rate settles instead of flapping.
struct RateAdapter {
  // Below this the server is dropping samples: it discards its oldest blocks when the client cannot keep up, and TCP then delivers
  // everything it was given, so a steady 88% link is 12% of the samples missing, not a slow stream. A one-hour soak on a double
  // Wi-Fi hop sat at 80-92% for the whole hour, so 85% never stepped down; healthy rates read 95% and above. The status text uses the same line.
  static constexpr uint32_t kBehindPct = 93;
  static constexpr uint32_t kHealthyPct = 95;     // needed to count toward a step up
  static constexpr uint32_t kBehindTicks = 4;     // consecutive samples before stepping down
  static constexpr uint32_t kUpTicks = 30;        // about 60 s healthy before the first probe
  static constexpr uint32_t kUpTicksMax = 480;    // about 16 min between probes after failures
  static constexpr uint32_t kLadder[] = {1440000, 960000, 240000};
  static constexpr uint8_t kLadderSize = 3;
  enum class Change : uint8_t { none, down, up };

  uint8_t step = 0;                 // rungs below the wanted rate
  uint8_t behind_ticks = 0;
  uint32_t healthy_ticks = 0;
  uint32_t up_wait = kUpTicks;
  bool probing = false;             // just stepped up and not yet proven

  static uint8_t base_index(uint32_t wanted_sps) {
    uint8_t i = 0;
    while (i + 1 < kLadderSize && kLadder[i] > wanted_sps) ++i;
    return i;
  }
  uint32_t rate(uint32_t wanted_sps) const {
    const uint8_t i = base_index(wanted_sps);
    const uint8_t at = (i + step < kLadderSize) ? i + step : kLadderSize - 1;
    // Only a ladder rate at or below the wanted one is used; a wanted rate off the ladder
    // (ADS-B, lab overrides) stays as asked.
    return (step == 0 && kLadder[i] != wanted_sps) ? wanted_sps : kLadder[at];
  }
  void reset() { step = 0; behind_ticks = 0; healthy_ticks = 0; up_wait = kUpTicks; probing = false; }
  // `link_pct` 0 means not measured yet. `floor_sps` is the lowest rate the consumer accepts.
  // Returns the change just made; on down or up the stream should restart at rate().
  Change note(uint32_t link_pct, uint32_t wanted_sps, uint32_t floor_sps) {
    if (link_pct == 0) { behind_ticks = 0; return Change::none; }
    if (link_pct >= kBehindPct) {
      behind_ticks = 0;
      if (step == 0) { healthy_ticks = 0; probing = false; return Change::none; }
      if (link_pct < kHealthyPct) { healthy_ticks = 0; return Change::none; }
      if (++healthy_ticks < up_wait) return Change::none;
      healthy_ticks = 0;
      --step;
      probing = true;
      if (step == 0) up_wait = kUpTicks;  // fully recovered
      return Change::up;
    }
    healthy_ticks = 0;
    if (++behind_ticks < kBehindTicks) return Change::none;
    behind_ticks = 0;
    const uint8_t i = base_index(wanted_sps);
    if (i + step + 1 >= kLadderSize || kLadder[i + step + 1] < floor_sps) return Change::none;
    if (probing) up_wait = (up_wait * 2 > kUpTicksMax) ? kUpTicksMax : up_wait * 2;
    probing = false;
    ++step;
    return Change::down;
  }
};

}  // namespace orcsdr::rtl_tcp
