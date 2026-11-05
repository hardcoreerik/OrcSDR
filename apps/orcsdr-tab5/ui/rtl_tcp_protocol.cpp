#include "rtl_tcp_protocol.hpp"

#include <cstring>

namespace orcsdr::rtl_tcp {
namespace {

constexpr uint8_t kMagic[4] = {'R', 'T', 'L', '0'};

uint32_t read_be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

}  // namespace

void encode_command(Command command, uint32_t parameter, uint8_t out[kCommandBytes]) {
  out[0] = static_cast<uint8_t>(command);
  out[1] = static_cast<uint8_t>(parameter >> 24);
  out[2] = static_cast<uint8_t>(parameter >> 16);
  out[3] = static_cast<uint8_t>(parameter >> 8);
  out[4] = static_cast<uint8_t>(parameter);
}

void encode_greeting(uint32_t tuner_type_raw, uint32_t gain_count, uint8_t out[kGreetingBytes]) {
  std::memcpy(out, kMagic, sizeof(kMagic));
  for (int i = 0; i < 4; ++i) {
    out[4 + i] = static_cast<uint8_t>(tuner_type_raw >> (24 - 8 * i));
    out[8 + i] = static_cast<uint8_t>(gain_count >> (24 - 8 * i));
  }
}

bool CommandParser::push(uint8_t byte, RawCommand* out) {
  raw_[have_++] = byte;
  if (have_ < kCommandBytes) return false;
  have_ = 0;
  if (out != nullptr) {
    out->id = raw_[0];
    out->parameter = read_be32(raw_ + 1);
  }
  return true;
}

uint32_t parameter_signed(int32_t value) { return static_cast<uint32_t>(value); }

uint32_t parameter_if_gain(uint16_t stage, int16_t gain_tenths_db) {
  return (static_cast<uint32_t>(stage) << 16) | static_cast<uint16_t>(gain_tenths_db);
}

const char* tuner_type_name(uint32_t tuner_type_raw) {
  switch (tuner_type_raw) {
    case 1: return "E4000";
    case 2: return "FC0012";
    case 3: return "FC0013";
    case 4: return "FC2580";
    case 5: return "R820T/R820T2";
    case 6: return "R828D";
    default: return "unknown";
  }
}

void GreetingParser::reset() {
  have_ = 0;
  status_ = GreetingStatus::need_more;
  greeting_ = Greeting{};
}

GreetingStatus GreetingParser::feed(const uint8_t* data, size_t length, size_t* consumed) {
  size_t used = 0;
  if (status_ == GreetingStatus::need_more) {
    while (used < length && have_ < kGreetingBytes) {
      const uint8_t byte = data[used];
      if (have_ < sizeof(kMagic) && byte != kMagic[have_]) {
        status_ = GreetingStatus::bad_magic;
        break;
      }
      raw_[have_++] = byte;
      ++used;
    }
    if (status_ == GreetingStatus::need_more && have_ == kGreetingBytes) {
      greeting_.tuner_type_raw = read_be32(raw_ + 4);
      greeting_.gain_count = read_be32(raw_ + 8);
      status_ = greeting_.gain_count > kMaxPlausibleGainCount ? GreetingStatus::bad_field
                                                              : GreetingStatus::ok;
    }
  }
  if (consumed != nullptr) *consumed = used;
  return status_;
}

size_t IqPairCarry::prepare(uint8_t* slot) {
  if (!has_) return 0;
  slot[0] = byte_;  // still held until commit(): a read that never completes keeps it
  return 1;
}

size_t IqPairCarry::commit(uint8_t* slot, size_t offset, size_t received) {
  const size_t total = offset + received;
  has_ = (total & 1u) != 0;
  if (!has_) return total;
  byte_ = slot[total - 1];
  return total - 1;
}

bool valid_host(const char* host) {
  if (host == nullptr) return false;
  size_t length = 0;
  for (; host[length] != '\0'; ++length) {
    const char c = host[length];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
    if (!ok || length >= kMaxHostChars) return false;
  }
  return length > 0 && host[0] != '-' && host[0] != '.';
}

bool reconnect_delay_ms(const BackoffPolicy& policy, uint32_t attempt, uint32_t* delay_ms) {
  if (attempt >= policy.max_attempts) return false;
  uint64_t delay = policy.initial_ms;
  for (uint32_t i = 0; i < attempt && delay < policy.max_ms; ++i) delay *= 2;
  if (delay > policy.max_ms) delay = policy.max_ms;
  if (delay_ms != nullptr) *delay_ms = static_cast<uint32_t>(delay);
  return true;
}

}  // namespace orcsdr::rtl_tcp
