#include "web_iq.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "eth_app.hpp"
#include "wifi_app.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "radio_service.hpp"
#include "sdkconfig.h"
#include "web_audio.hpp"
#include "web_control.hpp"
#include "web_server.hpp"

#define ORCSDR_WEB_IQ (CONFIG_HTTPD_WS_SUPPORT && CONFIG_LWIP_MAX_SOCKETS >= 16)

namespace web_iq {
#if !ORCSDR_WEB_IQ
Start start_channel(int, uint32_t, uint32_t, uint32_t, uint32_t, char* out, size_t cap) {
  std::snprintf(out, cap, "{\"ok\":false,\"error\":\"browser_iq_stream_not_built_into_this_firmware\"}");
  return Start::not_available;
}
void stop_channel() {}
void shutdown() {}
bool available() { return false; }
bool slot_open(Slot, uint32_t) { return false; }
bool slot_send(const uint8_t*, size_t) { return false; }
bool slot_connected() { return false; }
void slot_close() {}
#else
namespace {
constexpr char TAG[] = "WEB_IQ";
namespace radio = orcsdr::radio;
constexpr uint32_t kMagic = 0x3151494f;  // "OIQ1" little-endian, what the page checks
constexpr size_t kHeader = 28, kPairs = 2048;  // 4 KB frames: fewer packets per second matters on the Wi-Fi link
constexpr int64_t kTicketUs = 30LL * 1000 * 1000;

// Third-order CIC decimator for one component. Integers wrap on purpose: the comb stages cancel the wrap.
struct Cic {
  int32_t i1 = 0, i2 = 0, i3 = 0, d1 = 0, d2 = 0, d3 = 0;
};

struct Channel {
  int rx = 0;
  uint32_t in_rate = 0, center_hz = 0, out_rate = 0, decim = 0, restore_hz = 0;  // restore_hz: where the receiver was before the channel moved it
  float scale = 1.f;
  Cic ci, cq;
  uint32_t phase = 0, session = 0, seq = 0, first = 0;
  size_t fill = 0;
  uint8_t frame[kHeader + kPairs * 2];
  bool ended = false;
  int64_t start_us = 0;  // when the sink was added: blocks that were already queued with the old centre arrive for a moment after a retune
};

httpd_handle_t g_srv = nullptr;
std::atomic<int> g_fd{-1};
std::atomic<uint32_t> g_ticket_peer{0};
std::atomic<int64_t> g_ticket_until{0};
std::atomic<uint8_t> g_ticket_kind{0};      // Slot the ticket was issued for: the URI the browser opens must match it
std::atomic<bool> g_ticket_reusable{false};  // audio: the page may reconnect on its own while the session lasts
Channel* g_chan = nullptr;
radio::SinkHandle g_sink = -1;
uint32_t g_start_count = 0, g_origin_hz = 0, g_last_center = 0;

void put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}
void put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}

void send_frame(Channel& c, size_t pairs, bool playing) {
  const int fd = g_fd.load();
  if (fd < 0 || !g_srv) return;
  uint8_t* f = c.frame;
  put32(f, kMagic);
  put32(f + 4, c.session);
  put32(f + 8, c.seq++);
  put32(f + 12, c.first);
  put32(f + 16, c.out_rate);
  put16(f + 20, static_cast<uint16_t>(pairs));
  put16(f + 22, playing ? 1 : 0);
  put32(f + 24, 8);  // 8-bit unsigned I/Q
  httpd_ws_frame_t ws{};
  ws.type = HTTPD_WS_TYPE_BINARY;
  ws.final = true;
  ws.payload = f;
  ws.len = kHeader + pairs * 2;
  if (httpd_ws_send_frame_async(g_srv, fd, &ws) != ESP_OK) {
    g_fd = -1;  // the browser went away; the stream goes on being decimated and thrown away until the channel is stopped
    return;
  }
  c.first += static_cast<uint32_t>(pairs);
}

inline uint8_t out_byte(Cic& s, float scale) {
  const int32_t c1 = s.i3 - s.d1;
  s.d1 = s.i3;
  const int32_t c2 = c1 - s.d2;
  s.d2 = c1;
  const int32_t c3 = c2 - s.d3;
  s.d3 = c2;
  int v = static_cast<int>(static_cast<float>(c3) * scale + 128.5f);
  return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

void on_block(const radio::IqBlock& b, void* ctx) {
  Channel& c = *static_cast<Channel*>(ctx);
  if (c.ended || g_fd.load() < 0) return;
  if ((b.sample_rate != c.in_rate || b.center_hz != c.center_hz) && esp_timer_get_time() - c.start_us < 1500000) return;  // still draining blocks from before the retune
  if (b.sample_rate != c.in_rate || b.center_hz != c.center_hz) {  // someone else retuned or changed the rate: the window is no longer what the page was told
    c.fill = 0;
    send_frame(c, 0, false);
    c.ended = true;
    ESP_LOGW(TAG, "WEB_IQ_ENDED receiver_changed rate=%u center=%u", static_cast<unsigned>(b.sample_rate), static_cast<unsigned>(b.center_hz));
    return;
  }
  const uint8_t* p = b.data;
  if (c.decim == 1) {  // wide window at the receiver's own rate: the bytes go straight into frames
    for (size_t n = 0; n + 1 < b.bytes; n += 2) {
      uint8_t* o = c.frame + kHeader + 2 * c.fill;
      o[0] = p[n];
      o[1] = p[n + 1];
      if (++c.fill == kPairs) {
        send_frame(c, kPairs, true);
        c.fill = 0;
        if (g_fd.load() < 0) return;
      }
    }
    return;
  }
  for (size_t n = 0; n + 1 < b.bytes; n += 2) {
    const int32_t xi = 2 * p[n] - 255, xq = 2 * p[n + 1] - 255;
    c.ci.i1 += xi;
    c.ci.i2 += c.ci.i1;
    c.ci.i3 += c.ci.i2;
    c.cq.i1 += xq;
    c.cq.i2 += c.cq.i1;
    c.cq.i3 += c.cq.i2;
    if (++c.phase < c.decim) continue;
    c.phase = 0;
    uint8_t* o = c.frame + kHeader + 2 * c.fill;
    o[0] = out_byte(c.ci, c.scale);
    o[1] = out_byte(c.cq, c.scale);
    if (++c.fill == kPairs) {
      send_frame(c, kPairs, true);
      c.fill = 0;
      if (g_fd.load() < 0) return;
    }
  }
}

bool ticket_ok(httpd_req_t* req) {
  const char* want = g_ticket_kind.load() == static_cast<uint8_t>(Slot::audio) ? "/api/v1/fm/audio" : "/api/v1/fm/iq";
  if (g_ticket_kind.load() == 0 || std::strncmp(req->uri, want, std::strlen(want)) != 0) return false;
  return web_server::same_site_request(req) && g_ticket_peer.load() != 0 && g_ticket_until.load() >= esp_timer_get_time() && web_server::peer_address(req) == g_ticket_peer.load();
}

// Runs before the server answers the upgrade, so a refused client gets a real 403 and never a WebSocket.
esp_err_t ws_pre_handshake(httpd_req_t* req) {
  if (ticket_ok(req)) return ESP_OK;
  httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "channel not opened by a signed request");
  return ESP_FAIL;
}

esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {  // the handshake is done; this connection is the one allowed
    if (!ticket_ok(req)) return ESP_FAIL;  // lost the race for the single ticket: close
    if (!g_ticket_reusable.load()) g_ticket_peer = 0;  // single use (an audio ticket lasts for the session)
    const int newfd = httpd_req_to_sockfd(req);
    const int oldfd = g_fd.exchange(newfd);
    if (oldfd >= 0 && oldfd != newfd && g_srv) httpd_sess_trigger_close(g_srv, oldfd);  // a reconnect replaces a half-open earlier socket
    const int one = 1;
    setsockopt(g_fd.load(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));  // 1 KB frames: without this Nagle waits for the peer's delayed ACK between frames
    ESP_LOGI(TAG, "WEB_IQ_CLIENT_CONNECTED fd=%d", g_fd.load());
    return ESP_OK;
  }
  httpd_ws_frame_t f{};
  if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK) return ESP_FAIL;
  if (f.len > 0 && f.len <= 125) {
    uint8_t buf[126];
    f.payload = buf;
    httpd_ws_recv_frame(req, &f, f.len);  // anything the page says is ignored
  } else if (f.len > 125) {
    return ESP_FAIL;
  }
  if (f.type == HTTPD_WS_TYPE_CLOSE) g_fd = -1;
  return ESP_OK;
}

// With a custom close_fn the server no longer closes the socket itself: this must (a missing close() leaks one lwip socket per connection).
void on_close(httpd_handle_t, int fd) {
  int expected = fd;
  g_fd.compare_exchange_strong(expected, -1);
  close(fd);
}

bool ensure_server() {
  if (g_srv) return true;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;
  config.ctrl_port = 32769;  // the main server uses the default
  config.core_id = 0;
  config.stack_size = 6144;
  config.max_open_sockets = 2;
  config.max_uri_handlers = 2;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 3;
  config.close_fn = on_close;
  if (httpd_start(&g_srv, &config) != ESP_OK) {
    g_srv = nullptr;
    return false;
  }
  httpd_uri_t ws{};
  ws.uri = "/api/v1/fm/iq";
  ws.method = HTTP_GET;
  ws.handler = ws_handler;
  ws.is_websocket = true;
  ws.ws_pre_handshake_cb = ws_pre_handshake;
  httpd_register_uri_handler(g_srv, &ws);
  ws.uri = "/api/v1/fm/audio";
  httpd_register_uri_handler(g_srv, &ws);
  return true;
}

void drop_slot() {
  g_ticket_peer = 0;
  g_ticket_kind = 0;
  g_ticket_reusable = false;
  const int fd = g_fd.exchange(-1);
  if (fd >= 0 && g_srv) {
    httpd_ws_frame_t bye{};  // a proper WebSocket close first, so the browser closes its side too, then make sure the server's side goes
    bye.type = HTTPD_WS_TYPE_CLOSE;
    httpd_ws_send_frame_async(g_srv, fd, &bye);
    httpd_sess_trigger_close(g_srv, fd);
  }
}

void end_stream() {
  if (g_sink >= 0) {
    if (!radio::remove_sink(g_sink)) {
      ESP_LOGW(TAG, "WEB_IQ_SINK_QUARANTINED the receiver service could not stop the sink cleanly; the channel buffer is kept");
      g_sink = -1;
      return;  // never free what a quarantined sink may still touch
    }
    g_sink = -1;
  }
  if (g_chan && g_chan->restore_hz) {  // put the receiver back unless somebody else has moved it since
    radio::ReceiverInfo ri;
    if (radio::receiver_info(g_chan->rx, &ri) && ri.frequency_hz == g_chan->center_hz) radio::tune_to(g_chan->rx, g_chan->restore_hz);
  }
  heap_caps_free(g_chan);
  g_chan = nullptr;
  if (g_ticket_kind.load() != static_cast<uint8_t>(Slot::audio)) drop_slot();  // an audio session's socket is not ours to close
}
}  // namespace

bool available() { return true; }

// What the active link can carry for one stream, Mbit/s (the same figures as the rtl_tcp server; the Wi-Fi one is measured sustained, about 5).
uint32_t link_budget_mbit() {
  if (eth_app::status().got_ip) return 85;
  return wifi_app::status().connected ? 5 : 0;
}

Start start_channel(int rx, uint32_t hz, uint32_t wide_center_hz, uint32_t wide_sps, uint32_t peer, char* out, size_t cap) {
  auto fail = [&](Start s, const char* msg) {
    std::snprintf(out, cap, "{\"ok\":false,\"error\":\"%s\"}", msg);
    return s;
  };
  radio::ReceiverInfo ri;
  if (!radio::receiver_info(rx, &ri) || ri.state != 2) return fail(Start::not_available, "receiver_not_streaming");
  const uint32_t in_rate = ri.sample_rate;
  uint32_t decim = 0, out_rate = 0, center = 0;
  int64_t offset = 0;
  if (wide_center_hz || wide_sps) {
    if (!wide_center_hz || !wide_sps || wide_center_hz < web_control::tune_min_hz(rx)) return fail(Start::bad_request, "bad_wide_window");
    if (wide_sps > in_rate || in_rate % wide_sps != 0 || in_rate / wide_sps > 16) return fail(Start::not_available, "rate_not_reachable_from_the_receiver_rate");
    decim = in_rate / wide_sps;
    out_rate = wide_sps;
    center = wide_center_hz;
    offset = static_cast<int64_t>(hz) - static_cast<int64_t>(center);
    if (static_cast<uint64_t>(out_rate) * 16 > static_cast<uint64_t>(link_budget_mbit()) * 1000000ull) return fail(Start::not_available, "link_too_slow_for_this_rate");
  } else {
    decim = (in_rate + 150000) / 300000;  // whatever rate the receiver runs: the nearest whole division to 300 kS/s (2.4 MS/s -> 8, 960 kS/s -> 3)
    if (decim < 1) decim = 1;
    if (decim > 16) return fail(Start::not_available, "receiver_rate_too_high_for_channel");
    out_rate = in_rate / decim;
    offset = out_rate / 6;
    if (hz < offset + web_control::tune_min_hz(rx)) return fail(Start::bad_request, "bad_hz");
    center = hz - static_cast<uint32_t>(offset);  // the channel sits `offset` above the window centre, away from the DC spike
  }

  web_audio::stop();  // one listener slot: a channel replaces the node's audio session too
  end_stream();       // ...and a new channel replaces the old one
  if (!ensure_server()) return fail(Start::not_available, "ws_server_start_failed");
  Channel* c = static_cast<Channel*>(heap_caps_calloc(1, sizeof(Channel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!c) return fail(Start::not_available, "no_memory");
  c->rx = rx;
  c->in_rate = in_rate;
  c->center_hz = center;
  // Where the receiver really was: if it still reads as the last channel's window (the retune back is not visible yet) the earlier original is kept.
  c->restore_hz = (g_origin_hz && ri.frequency_hz == g_last_center) ? g_origin_hz : ri.frequency_hz;
  g_origin_hz = c->restore_hz;
  g_last_center = center;
  c->out_rate = out_rate;
  c->decim = decim;
  float gain = 1.f;
  for (uint32_t i = 0; i < 3; ++i) gain *= static_cast<float>(decim);
  c->scale = 1.f / (2.f * gain);  // doubled input units (2x - 255) times CIC gain -> counts
  c->session = esp_random();
  g_chan = c;
  g_ticket_peer = peer ? peer : 1;
  g_ticket_until = esp_timer_get_time() + kTicketUs;
  g_ticket_reusable = false;
  g_ticket_kind = static_cast<uint8_t>(Slot::iq);

  radio::tune_to(rx, center);
  // The block that the receiver delivers after the retune carries the new centre; blocks before it do not match and end the channel, so wait for the retune to be visible.
  const int64_t deadline = esp_timer_get_time() + 3000000;
  radio::ReceiverInfo now;
  while (esp_timer_get_time() < deadline) {
    if (radio::receiver_info(rx, &now) && now.frequency_hz == center && now.state == 2) break;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  radio::SinkSpec spec;
  spec.name = "webiq";
  spec.fn = on_block;
  spec.context = c;
  spec.kind = radio::SinkKind::queued;
  spec.receiver = static_cast<uint8_t>(rx);
  spec.queue_bytes = 512 * 1024;
  spec.task_core = 0;
  spec.task_priority = 5;  // above the screen task (4): on a board with a display it would otherwise starve this task
  c->start_us = esp_timer_get_time();
  g_sink = radio::add_sink(spec);
  if (g_sink < 0) {
    heap_caps_free(c);
    g_chan = nullptr;
    return fail(Start::not_available, "no_sink_slot");
  }
  ++g_start_count;
  ESP_LOGI(TAG, "WEB_IQ_STARTED rx=%d hz=%u window_center=%u in=%u out=%u decim=%u", rx, static_cast<unsigned>(hz), static_cast<unsigned>(center), static_cast<unsigned>(in_rate),
           static_cast<unsigned>(out_rate), static_cast<unsigned>(decim));
  std::snprintf(out, cap, "{\"ok\":true,\"index\":%d,\"hz\":%u,\"center_hz\":%u,\"sps\":%u,\"offset_hz\":%d,\"bits\":8,\"ws_port\":81}", rx, static_cast<unsigned>(hz),
                static_cast<unsigned>(center), static_cast<unsigned>(out_rate), static_cast<int>(offset));
  return Start::ok;
}

void stop_channel() { end_stream(); }

bool slot_open(Slot kind, uint32_t peer) {
  if (!ensure_server()) return false;
  g_ticket_reusable = kind == Slot::audio;
  g_ticket_kind = static_cast<uint8_t>(kind);
  g_ticket_until = g_ticket_reusable.load() ? INT64_MAX : esp_timer_get_time() + kTicketUs;
  g_ticket_peer = peer ? peer : 1;
  return true;
}

bool slot_send(const uint8_t* data, size_t len) {
  const int fd = g_fd.load();
  if (fd < 0 || !g_srv) return false;
  httpd_ws_frame_t ws{};
  ws.type = HTTPD_WS_TYPE_BINARY;
  ws.final = true;
  ws.payload = const_cast<uint8_t*>(data);
  ws.len = len;
  if (httpd_ws_send_frame_async(g_srv, fd, &ws) != ESP_OK) {
    int expected = fd;
    g_fd.compare_exchange_strong(expected, -1);  // the browser went away; on_close finishes the job
    return false;
  }
  return true;
}

bool slot_connected() { return g_fd.load() >= 0; }

void slot_close() { drop_slot(); }

void shutdown() {
  web_audio::stop();
  end_stream();
  if (g_srv) {
    httpd_stop(g_srv);
    g_srv = nullptr;
  }
}
#endif
}  // namespace web_iq
