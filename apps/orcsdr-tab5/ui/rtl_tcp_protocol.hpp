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

// Nominal manual tuner gain steps, in tenths of a dB, for the tuner type an rtl_tcp greeting reports. The greeting carries only the type and a
// COUNT of steps, never the values, so a client that wants to offer a gain control needs the table. Known: R820T and R828D (types 5 and 6, 29 steps)
// and E4000 (type 1, 14 steps). Returns the number of steps written (0 when the type is unknown or the reported count does not match the table, so a
// variant is not given a wrong table). With max == 0 it only reports the count.
size_t nominal_gain_steps(uint32_t tuner_type_raw, uint32_t reported_count, int* out, size_t max);

// Bounded exponential reconnect delay.
struct BackoffPolicy {
  uint32_t initial_ms = 500;
  uint32_t max_ms = 15000;
  uint32_t max_attempts = 8;
};
// `attempt` counts from 0. Returns false once the attempts are spent.
bool reconnect_delay_ms(const BackoffPolicy& policy, uint32_t attempt, uint32_t* delay_ms);

// Steps the sample rate down when the link cannot carry it, so a weak Wi-Fi link gives live audio
// with fewer samples lost, and climbs back when the link has room. One call per link-ratio sample
// (about every 2 s). Pure, so it is host-tested against readings recorded on the real link.
// The rates are an explicit ladder because every rung must be a whole multiple of the 240 kS/s
// demodulation rate AND a rate the RTL2832 can run (225001-300000 and 900001-3200000 S/s only, so
// 480 kS/s is not possible on a real dongle).
//
// Why a windowed mean, not single readings: below 100% the server is dropping samples (it discards its oldest blocks
// when the client cannot keep up and TCP delivers everything it was given), but one 2 s reading is noisy. Measured on a
// double Wi-Fi hop: 1.44 MS/s read 80-92% for an hour (mean 87, about 11% of samples missing); 0.96 MS/s read 79-97%
// (mean 94, acceptable); 0.24 MS/s read 99-101%. A rule on single readings either never fires (85%) or sits in the
// middle of the acceptable rate's noise (93%), so decisions use the mean of the last kWindow readings, with a fast
// path for a collapsing link. A step up also needs the mean to be clearly healthy, which 0.96 MS/s on that link never is,
// so it settles there instead of probing 1.44 MS/s over and over. A probe that falls behind again doubles the wait.
struct RateAdapter {
  static constexpr uint32_t kBehindPct = 90;       // a single reading below this is labelled BEHIND in the status text
  static constexpr uint32_t kWindow = 8;           // readings averaged for decisions (about 16 s)
  static constexpr uint32_t kStepDownMeanPct = 91; // mean below this: the server is dropping samples
  static constexpr uint32_t kSeverePct = 70;       // a collapsing link does not wait for a full window ...
  static constexpr uint32_t kSevereTicks = 3;      // ... three readings in a row below this step down
  static constexpr uint32_t kHealthyMeanPct = 97;  // mean at or above this counts toward a step up
  static constexpr uint32_t kUpTicks = 30;         // about 60 s healthy before the first probe
  static constexpr uint32_t kUpTicksMax = 480;     // about 16 min between probes after failures
  static constexpr uint32_t kLadder[] = {1440000, 960000, 240000};
  static constexpr uint8_t kLadderSize = 3;
  enum class Change : uint8_t { none, down, up };

  uint8_t step = 0;                 // rungs below the wanted rate
  uint8_t window[kWindow]{};        // the last readings at the current rate
  uint8_t window_count = 0;
  uint8_t window_pos = 0;
  uint8_t severe_ticks = 0;
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
  void clear_window() { window_count = 0; window_pos = 0; severe_ticks = 0; }
  void reset() { step = 0; healthy_ticks = 0; up_wait = kUpTicks; probing = false; clear_window(); }
  uint32_t window_mean() const {
    if (window_count == 0) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < window_count; ++i) sum += window[i];
    return sum / window_count;
  }
  // `link_pct` 0 means not measured yet. `floor_sps` is the lowest rate the consumer accepts.
  // Returns the change just made; on down or up the stream should restart at rate().
  Change note(uint32_t link_pct, uint32_t wanted_sps, uint32_t floor_sps) {
    if (link_pct == 0) { clear_window(); return Change::none; }
    window[window_pos] = static_cast<uint8_t>(link_pct > 255 ? 255 : link_pct);
    window_pos = static_cast<uint8_t>((window_pos + 1) % kWindow);
    if (window_count < kWindow) ++window_count;
    severe_ticks = link_pct < kSeverePct ? static_cast<uint8_t>(severe_ticks + 1) : 0;
    const bool full = window_count == kWindow;
    const uint32_t mean = window_mean();
    if (severe_ticks >= kSevereTicks || (full && mean < kStepDownMeanPct)) {
      healthy_ticks = 0;
      const uint8_t i = base_index(wanted_sps);
      if (i + step + 1 >= kLadderSize || kLadder[i + step + 1] < floor_sps) return Change::none;  // nothing lower to go to
      if (probing) up_wait = (up_wait * 2 > kUpTicksMax) ? kUpTicksMax : up_wait * 2;
      probing = false;
      ++step;
      clear_window();                                  // judge the new rate on its own readings
      return Change::down;
    }
    if (step == 0) { healthy_ticks = 0; probing = false; return Change::none; }
    if (!(full && mean >= kHealthyMeanPct)) { healthy_ticks = 0; return Change::none; }
    if (++healthy_ticks < up_wait) return Change::none;
    healthy_ticks = 0;
    --step;
    probing = true;
    if (step == 0) up_wait = kUpTicks;                 // fully recovered
    clear_window();
    return Change::up;
  }
};

}  // namespace orcsdr::rtl_tcp
