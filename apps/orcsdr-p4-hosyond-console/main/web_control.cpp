#include "web_control.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ORCSDR_WEB_CONTROL_HOST_TEST)
#include "home_action_api.hpp"
#else
#include "home_app.hpp"
#endif

namespace web_control {
namespace {
struct QueryValue {
  char bytes[64]{};
  bool present = false;
};

bool hex_digit(char c, unsigned* value) {
  if (c >= '0' && c <= '9') *value = static_cast<unsigned>(c - '0');
  else if (c >= 'a' && c <= 'f') *value = static_cast<unsigned>(c - 'a' + 10);
  else if (c >= 'A' && c <= 'F') *value = static_cast<unsigned>(c - 'A' + 10);
  else return false;
  return true;
}

bool decode(const char* begin, size_t length, char* out, size_t cap) {
  if (!cap || length >= cap) return false;
  size_t used = 0;
  for (size_t i = 0; i < length; ++i) {
    unsigned char c = static_cast<unsigned char>(begin[i]);
    if (c == '+') c = ' ';
    else if (c == '%') {
      unsigned hi = 0, lo = 0;
      if (i + 2 >= length || !hex_digit(begin[i + 1], &hi) || !hex_digit(begin[i + 2], &lo)) return false;
      c = static_cast<unsigned char>((hi << 4) | lo);
      i += 2;
    }
    if (!c || c < 0x20 || c == 0x7f || used + 1 >= cap) return false;
    out[used++] = static_cast<char>(c);
  }
  out[used] = '\0';
  return true;
}

bool query_value(const char* query, const char* wanted, QueryValue* value) {
  if (!query || !wanted || !value || std::strlen(query) > 256) return false;
  const char* p = query;
  while (*p) {
    const char* amp = std::strchr(p, '&');
    const size_t pair_length = amp ? static_cast<size_t>(amp - p) : std::strlen(p);
    const char* equal = static_cast<const char*>(std::memchr(p, '=', pair_length));
    const size_t key_length = equal ? static_cast<size_t>(equal - p) : pair_length;
    char key[32], decoded[64];
    if (!decode(p, key_length, key, sizeof(key))) return false;
    const size_t value_length = equal ? pair_length - key_length - 1 : 0;
    if (!decode(equal ? equal + 1 : p + pair_length, value_length, decoded, sizeof(decoded))) return false;
    if (std::strcmp(key, wanted) == 0) {
      if (value->present) return false;
      value->present = true;
      std::memcpy(value->bytes, decoded, std::strlen(decoded) + 1);
    }
    p = amp ? amp + 1 : p + pair_length;
    if (amp && !*p) return false;
  }
  return true;
}

bool parse_u32(const QueryValue& value, uint32_t* out) {
  if (!value.present || !value.bytes[0]) return false;
  uint64_t n = 0;
  for (const char* p = value.bytes; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    n = n * 10 + static_cast<unsigned>(*p - '0');
    if (n > UINT32_MAX) return false;
  }
  *out = static_cast<uint32_t>(n);
  return true;
}

bool allowed_home_action(const char* action, bool* needs_value) {
  struct Entry { const char* name; bool value; };
  static constexpr Entry actions[] = {
      {"tune", true}, {"step_up", false}, {"step_down", false},
      {"step_size_up", false}, {"step_size_down", false},
      {"span_up", false}, {"span_down", false}, {"mode_set", true},
      {"filter_set", true}, {"gain_auto", false}, {"gain_tenth_db", true},
      {"rtl_agc", true},
  };
  for (const Entry& e : actions) {
    if (std::strcmp(action, e.name) == 0) {
      *needs_value = e.value;
      return true;
    }
  }
  return false;
}

Result fail(char* out, size_t cap, Result result, const char* error) {
  if (cap) std::snprintf(out, cap, "{\"ok\":false,\"error\":\"%s\"}", error);
  return result;
}

bool known_unavailable(const char* route) {
  static constexpr const char* routes[] = {
      "receivers/tune", "receivers/rate", "tuner/gain", "tuner/agc",
      "fm", "channel", "channel/stop", "time", "freq_set", "volume_set",
      "rtl_tcp", "wifi", "settings",
  };
  for (const char* candidate : routes)
    if (std::strcmp(route, candidate) == 0) return true;
  return false;
}
}  // namespace

Result run(const char* route, const char* query, uint32_t peer, char* out, size_t cap) {
  (void)peer;  // Reserved for the independently authenticated future audio/IQ socket adapters.
  if (!out || !cap || !route) return Result::bad_request;
  out[0] = '\0';
  if (std::strcmp(route, "home") != 0) {
    if (known_unavailable(route)) return fail(out, cap, Result::not_available, "not_integrated");
    return Result::unknown_route;
  }

  QueryValue action_value, numeric_value;
  if (!query_value(query, "action", &action_value) || !action_value.present || !action_value.bytes[0] ||
      !query_value(query, "value", &numeric_value))
    return fail(out, cap, Result::bad_request, "bad_action_or_query");

  bool needs_value = false;
  if (!allowed_home_action(action_value.bytes, &needs_value))
    return fail(out, cap, Result::bad_request, "action_not_allowed");

  uint32_t value = 0;
  if ((needs_value && !parse_u32(numeric_value, &value)) || (!needs_value && numeric_value.present))
    return fail(out, cap, Result::bad_request, "bad_value");
  if ((std::strcmp(action_value.bytes, "tune") == 0 && value == 0) ||
      (std::strcmp(action_value.bytes, "mode_set") == 0 && value > 5) ||
      (std::strcmp(action_value.bytes, "rtl_agc") == 0 && value > 1) ||
      (std::strcmp(action_value.bytes, "filter_set") == 0 && value == 0))
    return fail(out, cap, Result::bad_request, "value_out_of_range");

  switch (home_app::request_web_action(action_value.bytes, value)) {
    case home_app::WebAction::ok:
      std::snprintf(out, cap, "{\"ok\":true,\"queued\":true}");
      return Result::ok;
    case home_app::WebAction::bad_action:
    case home_app::WebAction::bad_value:
      return fail(out, cap, Result::bad_request, "action_rejected");
    case home_app::WebAction::queue_full:
      return fail(out, cap, Result::not_available, "queue_full");
    case home_app::WebAction::not_available:
    default:
      return fail(out, cap, Result::not_available, "action_not_available");
  }
}
}  // namespace web_control
