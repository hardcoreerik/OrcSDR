#include "esp_rtl_tcp.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "rtl_tcp_proto.hpp"

namespace esp_rtl_tcp {
namespace {
constexpr char TAG[] = "RTLTCP";

SemaphoreHandle_t g_mutex = nullptr;  // guards everything below that a second task can touch
Config g_cfg;
Host* g_host = nullptr;
Stats g_stats;
volatile bool g_run = false, g_task_alive = false;
int g_listen = -1, g_fd = -1;  // g_fd: the connected client, -1 when none
bool g_streaming = false;      // the header is out and the host is delivering
bool g_have_ref = false;       // the first block of the session has defined what the client was told
uint32_t g_ref_rate = 0, g_ref_center = 0;
EndReason g_pending = EndReason::none;  // an end requested by push(); the session task acts on it
uint32_t g_gain_count = 0;              // the ladder length announced in this session's header

struct Lock {
  Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mutex); }
};

// Requests the end of the session. The caller holds the lock. The socket is shut so a blocked recv or send returns; the session task closes it.
void end_locked(EndReason reason) {
  if (g_pending != EndReason::none) return;
  g_pending = reason;
  g_streaming = false;
  if (g_fd >= 0) shutdown(g_fd, SHUT_RDWR);
}

void bump(uint32_t Stats::*field) {
  Lock lock;
  ++(g_stats.*field);
}

bool send_all(int fd, const uint8_t* data, size_t len) {
  size_t off = 0;
  while (off < len) {
    const int n = send(fd, data + off, len - off, 0);
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

// Handles one command. Returns the reason to end the session, or none.
EndReason handle(const proto::Command& c) {
  const proto::Op op = proto::classify(c.code);
  if (op == proto::Op::unknown) {
    bump(&Stats::commands_unknown);
    return EndReason::none;
  }
  if (proto::is_unsupported(op)) {
    bump(&Stats::commands_unsupported);
    ESP_LOGI(TAG, "RTLTCP_CMD_UNSUPPORTED code=0x%02x", c.code);
    return EndReason::none;
  }
  if (!g_cfg.allow_control) {
    bump(&Stats::commands_ignored);
    ESP_LOGI(TAG, "RTLTCP_CMD_IGNORED code=0x%02x param=%u (listen-only)", c.code, static_cast<unsigned>(c.param));
    return EndReason::none;
  }
  bool ok = false;
  switch (op) {
    case proto::Op::set_center: ok = g_host->set_center(c.param); break;
    case proto::Op::set_rate: {
      switch (g_host->set_rate(c.param)) {
        case RateResult::ok: {
          Lock lock;
          g_have_ref = false;  // the next block defines the new stream
          ok = true;
          break;
        }
        case RateResult::unsupported: return EndReason::rate_unsupported;
        case RateResult::exceeds_link: return EndReason::rate_exceeds_link;
        default: return EndReason::rate_change_failed;
      }
      break;
    }
    case proto::Op::gain_mode: ok = g_host->set_gain_mode(c.param != 0); break;
    case proto::Op::gain: ok = g_host->set_gain_tenth_db(static_cast<int32_t>(c.param)); break;
    case proto::Op::ppm: {
      const int32_t ppm = static_cast<int32_t>(c.param);
      ok = ppm >= -200 && ppm <= 200 && g_host->set_ppm(ppm);  // outside the range is refused, never wrapped or clamped
      break;
    }
    case proto::Op::rtl_agc: ok = g_host->set_rtl_agc(c.param != 0); break;
    case proto::Op::gain_index: ok = c.param < g_gain_count && g_host->set_gain_index(c.param); break;
    case proto::Op::bias_tee: ok = g_host->set_bias_tee(c.param != 0); break;
    default: break;
  }
  bump(ok ? &Stats::commands_applied : &Stats::commands_rejected);
  ESP_LOGI(TAG, "RTLTCP_CMD %s code=0x%02x param=%u", ok ? "APPLIED" : "REJECTED", c.code, static_cast<unsigned>(c.param));
  return EndReason::none;
}

void run_session(int fd, const char* peer) {
  {
    Lock lock;
    g_fd = fd;
    g_streaming = false;
    g_have_ref = false;
    g_pending = EndReason::none;
    g_stats.client_connected = true;
    g_stats.session_rate = g_stats.session_center_hz = 0;
    ++g_stats.clients_total;
    std::snprintf(g_stats.peer, sizeof(g_stats.peer), "%s", peer);
  }
  ESP_LOGI(TAG, "RTLTCP_SESSION_START peer=%s mode=%s", peer, g_cfg.allow_control ? "control" : "listen-only");

  const int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  timeval send_to{};
  send_to.tv_sec = static_cast<long>(g_cfg.send_timeout_ms / 1000);
  send_to.tv_usec = static_cast<long>((g_cfg.send_timeout_ms % 1000) * 1000);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_to, sizeof(send_to));
  timeval recv_to{};
  recv_to.tv_usec = 200 * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_to, sizeof(recv_to));

  EndReason reason = EndReason::none;
  bool began = false;
  g_gain_count = g_host->gain_count();
  uint8_t header[proto::kHeaderBytes];
  proto::encode_header(header, g_host->tuner_type(), g_gain_count);
  if (!send_all(fd, header, sizeof(header))) {
    reason = EndReason::socket_error;  // a failed header send ends the session (the original only prints)
  } else if (!g_host->session_begin()) {
    reason = EndReason::host_refused;
  } else {
    began = true;
    Lock lock;
    g_streaming = g_pending == EndReason::none;
  }

  proto::CommandStream commands;
  uint8_t buf[64];
  while (reason == EndReason::none && g_run) {
    {
      Lock lock;
      if (g_pending != EndReason::none) break;
    }
    const int n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      commands.feed(buf, static_cast<size_t>(n), [&](const proto::Command& c) {
        if (reason == EndReason::none) reason = handle(c);
      });
    } else if (n == 0) {
      reason = EndReason::client_closed;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      reason = EndReason::socket_error;
    }
  }

  shutdown(fd, SHUT_RDWR);  // aborts a send that push() is blocked in
  {
    Lock lock;
    if (g_pending != EndReason::none) reason = g_pending;  // push() saw it first: that is the real cause
    if (reason == EndReason::none) reason = EndReason::stopped;
    g_streaming = false;
    g_fd = -1;
    close(fd);  // under the lock: push() is never mid-send on a descriptor that is being closed
    g_stats.client_connected = false;
    g_stats.last_end = reason;
  }
  if (began) g_host->session_end();
  const Stats s = stats();
  ESP_LOGI(TAG, "RTLTCP_SESSION_END reason=%s blocks=%u bytes=%llu", end_reason_name(reason), static_cast<unsigned>(s.blocks_sent), static_cast<unsigned long long>(s.bytes_sent));
}

void listener(void*) {
  g_task_alive = true;
  while (g_run) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    const int fd = accept(g_listen, reinterpret_cast<sockaddr*>(&addr), &len);
    if (fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(10));  // accept timed out, or the listener was closed by stop()
      continue;
    }
    char peer[16] = {};
    inet_ntop(AF_INET, &addr.sin_addr, peer, sizeof(peer));
    if (g_cfg.allow_ip != 0 && addr.sin_addr.s_addr != g_cfg.allow_ip) {
      close(fd);
      bump(&Stats::clients_rejected);
      ESP_LOGW(TAG, "RTLTCP_CLIENT_REJECTED peer=%s (not the allowed address)", peer);
      continue;
    }
    run_session(fd, peer);
  }
  if (g_listen >= 0) close(g_listen);
  g_listen = -1;
  g_task_alive = false;
  vTaskDelete(nullptr);
}
}  // namespace

const char* end_reason_name(EndReason reason) {
  switch (reason) {
    case EndReason::client_closed: return "client_closed";
    case EndReason::socket_error: return "socket_error";
    case EndReason::slow_peer: return "slow_peer";
    case EndReason::receiver_changed: return "receiver_changed";
    case EndReason::rate_unsupported: return "rate_unsupported";
    case EndReason::rate_exceeds_link: return "rate_exceeds_link";
    case EndReason::rate_change_failed: return "rate_change_failed";
    case EndReason::host_refused: return "host_refused";
    case EndReason::stopped: return "stopped";
    default: return "none";
  }
}

bool start(const Config& config, Host& host) {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  if (!g_mutex || g_run || g_task_alive) return false;
  const int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (ls < 0) return false;
  const int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  timeval accept_to{};
  accept_to.tv_usec = 500 * 1000;
  setsockopt(ls, SOL_SOCKET, SO_RCVTIMEO, &accept_to, sizeof(accept_to));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config.port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(ls, 1) != 0) {  // backlog 1: a second client waits
    ESP_LOGE(TAG, "RTLTCP_LISTEN_FAILED port=%u errno=%d", config.port, errno);
    close(ls);
    return false;
  }
  {
    Lock lock;
    g_cfg = config;
    g_host = &host;
    g_stats = Stats{};
    g_stats.running = true;
    g_stats.control_allowed = config.allow_control;
    g_listen = ls;
    g_pending = EndReason::none;
    g_fd = -1;
    g_streaming = false;
  }
  g_run = true;
  if (xTaskCreatePinnedToCore(listener, "rtltcp", config.task_stack, nullptr, 4, nullptr, config.core) != pdPASS) {
    g_run = false;
    close(ls);
    g_listen = -1;
    return false;
  }
  ESP_LOGI(TAG, "RTLTCP_LISTEN port=%u mode=%s", config.port, config.allow_control ? "control" : "listen-only");
  return true;
}

void stop() {
  if (!g_mutex) return;
  g_run = false;
  {
    Lock lock;
    end_locked(EndReason::stopped);
  }
  if (g_listen >= 0) shutdown(g_listen, SHUT_RDWR);
  for (int i = 0; i < 300 && g_task_alive; ++i) vTaskDelay(pdMS_TO_TICKS(10));
  Lock lock;
  g_stats.running = false;
}

bool running() { return g_run; }

Stats stats() {
  if (!g_mutex) return Stats{};
  Lock lock;
  Stats s = g_stats;
  s.running = g_run;
  return s;
}

void push(const uint8_t* data, size_t bytes, uint32_t rate, uint32_t center_hz) {
  if (!g_mutex || !g_run) return;
  Lock lock;
  if (!g_streaming || g_fd < 0) return;
  if (!g_have_ref) {
    g_have_ref = true;
    g_ref_rate = rate;
    g_ref_center = center_hz;
    g_stats.session_rate = rate;
    g_stats.session_center_hz = center_hz;
  } else if (rate != g_ref_rate || center_hz != g_ref_center) {
    if (!g_cfg.allow_control) {
      end_locked(EndReason::receiver_changed);  // the client was told nothing about this change and cannot learn of it
      return;
    }
    g_ref_rate = rate;  // control mode: the most recent commander wins
    g_ref_center = center_hz;
    g_stats.session_rate = rate;
    g_stats.session_center_hz = center_hz;
  }
  size_t off = 0;
  while (off < bytes) {
    const int n = send(g_fd, data + off, bytes - off, 0);
    if (n <= 0) {
      end_locked(errno == EAGAIN || errno == EWOULDBLOCK ? EndReason::slow_peer : EndReason::socket_error);
      return;
    }
    off += static_cast<size_t>(n);
    g_stats.bytes_sent += static_cast<uint64_t>(n);
  }
  ++g_stats.blocks_sent;
}

}  // namespace esp_rtl_tcp
