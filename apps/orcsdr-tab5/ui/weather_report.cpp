#include "weather_report.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::weather {
namespace {

class Writer {
 public:
  Writer(char* out, size_t cap) : out_(out), cap_(cap) { if (cap_) out_[0] = '\0'; }
  bool append(const char* s) {
    if (!ok_ || !s) return false;
    const size_t n = std::strlen(s);
    if (used_ + n >= cap_) return ok_ = false;
    std::memcpy(out_ + used_, s, n + 1);
    used_ += n;
    return true;
  }
  template <typename... A> bool format(const char* fmt, A... args) {
    if (!ok_) return false;
    const int n = std::snprintf(out_ + used_, cap_ > used_ ? cap_ - used_ : 0, fmt, args...);
    if (n < 0 || static_cast<size_t>(n) >= cap_ - used_) return ok_ = false;
    used_ += static_cast<size_t>(n);
    return true;
  }
  bool ok() const { return ok_; }
 private:
  char* out_;
  size_t cap_;
  size_t used_ = 0;
  bool ok_ = true;
};

bool append_json_string(Writer& w, const char* s) {
  if (!w.append("\"")) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s ? s : ""); *p; ++p) {
    char tmp[8]{};
    switch (*p) {
      case '\\': if (!w.append("\\\\")) return false; break;
      case '"': if (!w.append("\\\"")) return false; break;
      case '\n': if (!w.append("\\n")) return false; break;
      case '\r': if (!w.append("\\r")) return false; break;
      case '\t': if (!w.append("\\t")) return false; break;
      default:
        if (*p < 0x20) { std::snprintf(tmp, sizeof(tmp), "\\u%04x", *p); if (!w.append(tmp)) return false; }
        else { tmp[0] = static_cast<char>(*p); tmp[1] = '\0'; if (!w.append(tmp)) return false; }
    }
  }
  return w.append("\"");
}

bool append_csv(Writer& w, const char* s) {
  if (!w.append("\"")) return false;
  for (const char* p = s ? s : ""; *p; ++p) {
    char ch[2] = {*p, '\0'};
    if (*p == '"' && !w.append("\"")) return false;
    if (!w.append(ch)) return false;
  }
  return w.append("\"");
}

bool append_html(Writer& w, const char* s) {
  for (const char* p = s ? s : ""; *p; ++p) {
    switch (*p) {
      case '&': if (!w.append("&amp;")) return false; break;
      case '<': if (!w.append("&lt;")) return false; break;
      case '>': if (!w.append("&gt;")) return false; break;
      case '"': if (!w.append("&quot;")) return false; break;
      default: { char ch[2] = {*p, '\0'}; if (!w.append(ch)) return false; }
    }
  }
  return true;
}
}

const char* online_policy_label(OnlinePolicy policy) {
  switch (policy) {
    case OnlinePolicy::disabled: return "disabled";
    case OnlinePolicy::manual: return "manual";
    case OnlinePolicy::automatic: return "automatic";
  }
  return "disabled";
}

bool encode_report_json(const ReportSnapshot& r, char* output, size_t capacity) {
  Writer w(output, capacity);
  if (!w.format("{\"version\":1,\"wallclock_valid\":%s,\"created_utc\":%u,\"created_uptime_ms\":%u,\"online_policy\":\"%s\",\"location\":",
                r.wallclock_valid ? "true" : "false", r.created_utc, r.created_uptime_ms,
                online_policy_label(r.online_policy))) return false;
  if (!append_json_string(w, r.location_label)) return false;
  if (!w.format(",\"noaa\":{\"valid\":%s,\"source\":\"RF\",\"frequency_hz\":%u,\"relative_dbfs\":%.2f,\"age_seconds\":%u}}",
                r.noaa_valid ? "true" : "false", r.noaa_frequency_hz,
                static_cast<double>(r.noaa_dbfs), r.noaa_age_seconds)) return false;
  return w.ok();
}

bool encode_report_csv(const ReportSnapshot& r, char* output, size_t capacity) {
  Writer w(output, capacity);
  if (!w.append("source,frequency_hz,relative_dbfs,age_seconds,valid,location\r\n")) return false;
  if (!w.format("RF,%u,%.2f,%u,%d,", r.noaa_frequency_hz,
                static_cast<double>(r.noaa_dbfs), r.noaa_age_seconds,
                r.noaa_valid ? 1 : 0)) return false;
  if (!append_csv(w, r.location_label) || !w.append("\r\n")) return false;
  return w.ok();
}

bool encode_report_html(const ReportSnapshot& r, char* output, size_t capacity) {
  Writer w(output, capacity);
  if (!w.append("<!doctype html><meta charset=\"utf-8\"><title>OrcSDR Weather Report</title><h1>OrcSDR Weather Report</h1><p>Mode: ")) return false;
  if (!w.append(r.online_policy == OnlinePolicy::disabled ? "OFFLINE" : "ONLINE ENRICHMENT")) return false;
  if (!w.append("</p><p>Location: ") || !append_html(w, r.location_label) || !w.append("</p>")) return false;
  if (r.noaa_valid &&
      !w.format("<p>NOAA RF: %.3f MHz, relative %.1f dBFS, age %u s</p>",
                r.noaa_frequency_hz / 1000000.0,
                static_cast<double>(r.noaa_dbfs), r.noaa_age_seconds))
    return false;
  if (!r.noaa_valid && !w.append("<p>NOAA RF: no current measurement</p>")) return false;
  return w.ok();
}

}  // namespace orcsdr::weather
