// Shared plumbing for the decoder adapters (not part of the public ABI).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <new>
#include <string>

#include "orcdec.h"

// A growable JSON object builder. Numbers are printed with fixed formats so every build prints the same text.
class Json {
 public:
  Json() { s_ = "{"; }
  Json& str(const char* key, const char* value) { sep(); key_(key); quote(value); return *this; }
  Json& num(const char* key, long long v) { sep(); key_(key); char b[32]; snprintf(b, sizeof b, "%lld", v); s_ += b; return *this; }
  Json& unum(const char* key, unsigned long long v) { sep(); key_(key); char b[32]; snprintf(b, sizeof b, "%llu", v); s_ += b; return *this; }
  Json& flt(const char* key, double v, int decimals) { sep(); key_(key); char b[40]; snprintf(b, sizeof b, "%.*f", decimals, v); s_ += b; return *this; }
  Json& boolean(const char* key, bool v) { sep(); key_(key); s_ += v ? "true" : "false"; return *this; }
  Json& raw(const char* key, const std::string& fragment) { sep(); key_(key); s_ += fragment; return *this; }   // a pre-built JSON value
  const std::string& done() { if (s_.back() != '}') s_ += '}'; return s_; }

 private:
  void sep() { if (s_.size() > 1) s_ += ','; }
  void key_(const char* k) { quote(k); s_ += ':'; }
  void quote(const char* v) {
    s_ += '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(v); *p; ++p) {
      if (*p == '"' || *p == '\\') { s_ += '\\'; s_ += static_cast<char>(*p); }
      else if (*p < 0x20 || *p >= 0x7f) { char b[8]; snprintf(b, sizeof b, "\\u%04x", *p); s_ += b; }
      else s_ += static_cast<char>(*p);
    }
    s_ += '"';
  }
  std::string s_;
};

struct orcdec {
  virtual ~orcdec() = default;
  virtual void reset() = 0;
  virtual int feed(const void* data, size_t bytes) = 0;
  virtual int command(const char* command, uint64_t arg) = 0;
  virtual const void* attachment(uint32_t, size_t*) { return nullptr; }

  void push(const std::string& event) {
    if (events.size() >= kMaxEvents) { ++dropped; return; }
    events.push_back(event);
  }
  static constexpr size_t kMaxEvents = 512;
  std::deque<std::string> events;
  uint32_t dropped = 0;
  uint32_t sequence = 0;   // per-decoder event counter, in the order events are produced
};

// "key=value;key=value" lookup. Returns `fallback` when absent.
inline std::string option(const char* options, const char* key, const char* fallback) {
  if (!options) return fallback;
  const size_t n = strlen(key);
  for (const char* p = options; *p;) {
    const char* end = strchr(p, ';');
    const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    if (len > n && !strncmp(p, key, n) && p[n] == '=') return std::string(p + n + 1, len - n - 1);
    if (!end) break;
    p = end + 1;
  }
  return fallback;
}

orcdec* orcdec_make_pocsag(const char* options);
orcdec* orcdec_make_ft8(const char* options);
orcdec* orcdec_make_js8(const char* options);
orcdec* orcdec_make_lora(const char* options);
orcdec* orcdec_make_sstv(const char* options);
orcdec* orcdec_make_adsb(const char* options);
orcdec* orcdec_make_acars(const char* options);
orcdec* orcdec_make_p25(const char* options);
