#include "commands.hpp"
#include <cstdio>
#include <cstring>
#include "audio_service.hpp"
#include "esp_heap_caps.h"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "sd_card.hpp"
#include "web_server.hpp"
#include "wifi_app.hpp"

namespace orcsdr::commands {

// ---- result building ---------------------------------------------------------------------------------------------

Record* add_record(Result& r, const char* label) {
  if (r.count >= kMaxRecords) return nullptr;
  Record* rec = &r.records[r.count++];
  rec->label = label;
  rec->count = 0;
  return rec;
}

static Field* next_field(Record& rec, const char* name, Kind kind) {
  if (rec.count >= kMaxFields) return nullptr;
  Field* f = &rec.fields[rec.count++];
  f->name = name;
  f->kind = kind;
  f->precision = 0;
  f->quoted = false;
  f->u = 0;
  return f;
}

void put_int(Record& rec, const char* name, int64_t v) {
  if (Field* f = next_field(rec, name, Kind::integer)) f->i = v;
}
void put_uint(Record& rec, const char* name, uint64_t v) {
  if (Field* f = next_field(rec, name, Kind::unsigned_int)) f->u = v;
}
void put_real(Record& rec, const char* name, double v, int precision) {
  if (Field* f = next_field(rec, name, Kind::real)) {
    f->f = v;
    f->precision = static_cast<int8_t>(precision);
  }
}
void put_text(Result& r, Record& rec, const char* name, const char* v, bool quoted) {
  Field* f = next_field(rec, name, Kind::text);
  if (!f) return;
  const char* src = v ? v : "";
  const size_t need = std::strlen(src) + 1;
  if (r.arena_used + need > sizeof(r.arena)) {  // out of room: keep an empty string rather than overrun
    f->s = "";
  } else {
    char* dst = r.arena + r.arena_used;
    std::memcpy(dst, src, need);
    r.arena_used += need;
    f->s = dst;
  }
  f->quoted = quoted;
}

// ---- renderers ---------------------------------------------------------------------------------------------------

void print_serial(const Result& r) {
  for (int i = 0; i < r.count; ++i) {
    const Record& rec = r.records[i];
    std::printf("%s", rec.label);
    for (int j = 0; j < rec.count; ++j) {
      const Field& f = rec.fields[j];
      switch (f.kind) {
        case Kind::integer: std::printf(" %s=%lld", f.name, static_cast<long long>(f.i)); break;
        case Kind::unsigned_int: std::printf(" %s=%llu", f.name, static_cast<unsigned long long>(f.u)); break;
        case Kind::real: std::printf(" %s=%.*f", f.name, int(f.precision), f.f); break;
        case Kind::text: std::printf(f.quoted ? " %s=\"%s\"" : " %s=%s", f.name, f.s); break;
      }
    }
    std::printf("\n");
  }
}

namespace {
struct Out {
  char* p;
  size_t cap, len = 0;
  bool overflow = false;
  void put(const char* s, size_t n) {
    if (len + n + 1 > cap) {
      overflow = true;
      return;
    }
    std::memcpy(p + len, s, n);
    len += n;
    p[len] = '\0';
  }
  void put(const char* s) { put(s, std::strlen(s)); }
  void esc(const char* s) {
    put("\"");
    for (; *s; ++s) {
      const unsigned char c = static_cast<unsigned char>(*s);
      if (c == '"' || c == '\\') {
        const char two[2] = {'\\', static_cast<char>(c)};
        put(two, 2);
      } else if (c < 0x20) {
        char u[8];
        const int n = std::snprintf(u, sizeof(u), "\\u%04x", c);
        put(u, static_cast<size_t>(n));
      } else {
        put(reinterpret_cast<const char*>(&c), 1);
      }
    }
    put("\"");
  }
};
}  // namespace

size_t to_json(const Result& r, char* out, size_t cap) {
  Out o{out, cap};
  if (cap) out[0] = '\0';
  o.put(r.status == Status::ok ? "{\"ok\":true,\"records\":[" : "{\"ok\":false,\"records\":[");
  for (int i = 0; i < r.count; ++i) {
    const Record& rec = r.records[i];
    if (i) o.put(",");
    o.put("{\"_\":");
    o.esc(rec.label);
    for (int j = 0; j < rec.count; ++j) {
      const Field& f = rec.fields[j];
      o.put(",");
      o.esc(f.name);
      o.put(":");
      char num[40];
      switch (f.kind) {
        case Kind::integer: o.put(num, static_cast<size_t>(std::snprintf(num, sizeof(num), "%lld", static_cast<long long>(f.i)))); break;
        case Kind::unsigned_int: o.put(num, static_cast<size_t>(std::snprintf(num, sizeof(num), "%llu", static_cast<unsigned long long>(f.u)))); break;
        case Kind::real: o.put(num, static_cast<size_t>(std::snprintf(num, sizeof(num), "%.*f", int(f.precision), f.f))); break;
        case Kind::text: o.esc(f.s); break;
      }
    }
    o.put("}");
  }
  o.put("]}");
  return o.overflow ? 0 : o.len;
}

// ---- the commands (read tier) ------------------------------------------------------------------------------------

namespace {
Status run_status(const Caller&, const Args&, Result& r) {
  const auto rs = radio::snapshot();
  Record* rec = add_record(r, "RTL_SDR_STATUS");
  put_int(*rec, "connected", rs.receiver_state >= 2 ? 1 : 0);
  put_int(*rec, "streaming", rs.receiver_state == 2 ? 1 : 0);
  put_uint(*rec, "effective_sps", rs.effective_sps);
  put_uint(*rec, "frequency_hz", rs.applied_hz);
  put_text(r, *rec, "driver", "rtl_sdr");
  return Status::ok;
}

Status run_freq(const Caller&, const Args&, Result& r) {
  const home_app::Status st = home_app::status();
  Record* rec = add_record(r, "RTL_FREQ_STATUS");
  put_text(r, *rec, "band", st.band);
  put_uint(*rec, "frequency_hz", st.frequency_hz);
  put_text(r, *rec, "mode", st.mode);
  return Status::ok;
}

Status run_signal(const Caller&, const Args&, Result& r) {
  const home_app::Status st = home_app::status();
  Record* rec = add_record(r, "RTL_SIGNAL_STATUS");
  put_text(r, *rec, "band", st.band);
  put_uint(*rec, "frequency_hz", st.frequency_hz);
  put_real(*rec, "signal_dbfs", static_cast<double>(radio::level_dbfs(0)), 1);
  return Status::ok;
}

Status run_volume(const Caller&, const Args&, Result& r) {
  Record* rec = add_record(r, "RTL_VOLUME_STATUS");
  put_uint(*rec, "volume", unsigned(home_app::status().volume * 32 / 255));
  return Status::ok;
}

Status run_mem(const Caller&, const Args&, Result& r) {
  Record* a = add_record(r, "RTL_MEM");
  put_uint(*a, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  put_uint(*a, "psram_largest", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  put_uint(*a, "internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  put_uint(*a, "internal_largest", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Record* d = add_record(r, "RTL_MEM_DMA");
  put_uint(*d, "dma_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  put_uint(*d, "dma_largest", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  return Status::ok;
}

Status run_rx(const Caller&, const Args&, Result& r) {
  Record* head = add_record(r, "RTL_RX_STATUS");
  put_int(*head, "max_receivers", radio::max_receivers());
  put_int(*head, "streaming", radio::receiver_count());
  for (int i = 0; i < radio::max_receivers(); ++i) {
    radio::ReceiverInfo ri;
    if (!radio::receiver_info(i, &ri)) continue;
    Record* rec = add_record(r, "RTL_RX");
    if (!rec) break;
    put_int(*rec, "index", ri.index);
    put_int(*rec, "state", ri.state);
    put_uint(*rec, "frequency_hz", ri.frequency_hz);
    put_uint(*rec, "sample_rate", ri.sample_rate);
    put_uint(*rec, "effective_sps", ri.effective_sps);
    put_uint(*rec, "drops", ri.drops);
    put_text(r, *rec, "usb_path", ri.usb_path, true);
    put_text(r, *rec, "device", ri.device, true);
  }
  return Status::ok;
}

Status run_wifi(const Caller&, const Args&, Result& r) {
  const wifi_app::Status w = wifi_app::status();
  Record* rec = add_record(r, "RTL_WIFI_STATUS");
  put_int(*rec, "started", w.started);
  put_int(*rec, "link_failed", w.link_failed);
  put_int(*rec, "connected", w.connected);
  put_int(*rec, "connecting", w.connecting);
  put_text(r, *rec, "ssid", w.ssid);
  put_text(r, *rec, "ip", w.ip);
  put_int(*rec, "rssi", int(w.rssi));
  put_text(r, *rec, "c6", w.c6_version);
  put_text(r, *rec, "stage", w.failure_stage);
  put_int(*rec, "code", w.failure_code);
  put_int(*rec, "saved", w.saved);
  return Status::ok;
}

Status run_sd(const Caller&, const Args&, Result& r) {
  const sd_card::Status s = sd_card::status();
  Record* rec = add_record(r, "RTL_SD_STATUS");
  put_int(*rec, "attempted", s.attempted);
  put_int(*rec, "mounted", s.mounted);
  put_uint(*rec, "total_mb", static_cast<unsigned long long>(s.total_bytes >> 20));
  return Status::ok;
}

Status run_audio(const Caller&, const Args&, Result& r) {
  const auto a = audio_service::status();
  Record* rec = add_record(r, "RTL_AUDIO_STATUS");
  put_int(*rec, "running", a.running);
  put_int(*rec, "muted", a.muted);
  put_uint(*rec, "volume", unsigned(a.volume));
  put_uint(*rec, "blocks", unsigned(a.blocks));
  put_uint(*rec, "dropped", unsigned(a.dropped_blocks));
  put_uint(*rec, "underruns", unsigned(a.underruns));
  put_real(*rec, "fill", double(a.buffer_fill), 2);
  put_real(*rec, "ppm", double(a.resample_ppm), 0);
  put_uint(*rec, "demod_us", unsigned(a.demod_us));
  put_uint(*rec, "write_us", unsigned(a.write_us));
  put_uint(*rec, "resample_us", unsigned(a.resample_us));
  return Status::ok;
}

Status run_ui(const Caller&, const Args&, Result& r) {
  const home_app::Status st = home_app::status();
  Record* rec = add_record(r, "RTL_UI_STATUS");
  put_text(r, *rec, "screen", home_app::screen_name());
  put_text(r, *rec, "band", st.band);
  put_uint(*rec, "frequency_hz", st.frequency_hz);
  put_int(*rec, "home_font", 1);
  put_int(*rec, "graphics", 1);
  put_int(*rec, "radio_ui", 1);
  put_text(r, *rec, "route", "m5gfx_scaled");
  return Status::ok;
}

Status run_web(const Caller&, const Args&, Result& r) {
  Record* rec = add_record(r, "RTL_WEB_STATUS");
  put_int(*rec, "enabled", web_server::enabled());
  put_int(*rec, "listening", web_server::listening());
  put_text(r, *rec, "url", web_server::url());
  return Status::ok;
}

const Command kCommands[] = {
    {"RTL_STATUS", "status", Access::read, run_status},
    {"RTL_FREQ", "freq", Access::read, run_freq},
    {"RTL_SIGNAL", "signal", Access::read, run_signal},
    {"RTL_VOLUME", "volume", Access::read, run_volume},
    {"RTL_MEM STATUS", "mem", Access::read, run_mem},
    {"RTL_RX STATUS", "rx", Access::read, run_rx},
    {"RTL_WIFI STATUS", "wifi", Access::read, run_wifi},
    {"RTL_SD STATUS", "sd", Access::read, run_sd},
    {"RTL_AUDIO STATUS", "audio", Access::read, run_audio},
    {"RTL_UI STATUS", "ui", Access::read, run_ui},
    {"RTL_WEB_STATUS", "web", Access::read, run_web},
    {"RTL_WEB", nullptr, Access::read, run_web},  // serial alias of RTL_WEB_STATUS (Tab5 command spec)
};
}  // namespace

const Command* commands(size_t* count) {
  if (count) *count = sizeof(kCommands) / sizeof(kCommands[0]);
  return kCommands;
}
const Command* find_serial(const char* line) {
  for (const Command& c : kCommands)
    if (std::strcmp(c.serial_line, line) == 0) return &c;
  return nullptr;
}
const Command* find_path(const char* path) {
  for (const Command& c : kCommands)
    if (c.http_path && std::strcmp(c.http_path, path) == 0) return &c;
  return nullptr;
}

Status run(const Command& cmd, const Caller& caller, const Args& args, Result& out) {
  out.count = 0;
  out.arena_used = 0;
  // Access is checked once, here, so a handler never has to remember to do it.
  if (cmd.access != Access::read && !caller.authed) return out.status = Status::auth_required;
  if (cmd.access == Access::admin && caller.via != Transport::serial) return out.status = Status::forbidden;
  return out.status = cmd.run(caller, args, out);
}

Result* serial_result() {
  // The result lives in PSRAM: nothing about it needs internal RAM, and internal RAM is the scarce resource here.
  static Result* result = static_cast<Result*>(heap_caps_calloc(1, sizeof(Result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return result;
}

bool run_serial(const char* line, const Caller& caller) {
  const Command* cmd = find_serial(line);
  if (!cmd) return false;
  Result* result = serial_result();
  if (!result) {
    std::printf("RTL_COMMAND_ERROR no_memory\n");
    return true;
  }
  if (run(*cmd, caller, Args{}, *result) == Status::ok) print_serial(*result);
  return true;
}

}  // namespace orcsdr::commands
