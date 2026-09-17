#include "web_audio_transport.hpp"
#include "web_audio_buffer.hpp"
#include "web_audio_protocol.hpp"
#include "web_command.hpp"

#include <atomic>
#include <cerrno>
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#if CONFIG_HTTPD_QUEUE_WORK_BLOCKING
#error Web audio requires nonblocking HTTP work submission
#endif

namespace orcsdr::web_audio {
namespace {
using namespace orcsdr::web_console;
EXT_RAM_BSS_ATTR AudioBuffer history;
EXT_RAM_BSS_ATTR int16_t pcm[kAudioPacketFrames];
EXT_RAM_BSS_ATTR uint8_t packet[kAudioPacketBytes];
std::atomic_flag history_lock = ATOMIC_FLAG_INIT;
std::atomic<bool> invalidate_pending{false}, stopping{true}, worker_alive{false}, queued{false};
std::atomic<uint32_t> clients{0}, queue_failures{0}, errors{0};
std::atomic<uint64_t> producer{0}, ring_written{0}, ring_read{0};
std::atomic<uint64_t> sent{0}, sent_frames{0}, contention{0}, dropped{0};
httpd_handle_t server = nullptr;
struct Client {
  int fd = -1;
  bool closing = false;
  int64_t send_deadline_us = 0;
  AudioBuffer::Cursor cursor{};
};
Client listeners[2]; // Only the HTTP task accesses these sessions and staging buffers.

void release_client(void* context) {
  auto* client = static_cast<Client*>(context);
  if (client->fd >= 0) {
    client->fd = -1;
    client->closing = false;
    clients.fetch_sub(1, std::memory_order_release);
  }
}

int complete_send(httpd_handle_t, int fd, const char* data, size_t length, int flags) {
  Client* client = nullptr;
  for (auto& item : listeners) if (item.fd == fd) { client = &item; break; }
  const int64_t deadline = client ? client->send_deadline_us : 0;
  size_t written = 0;
  while (written < length) {
    const int64_t remaining = deadline - esp_timer_get_time();
    if (deadline && remaining <= 0) return HTTPD_SOCK_ERR_TIMEOUT;
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval wait{static_cast<time_t>(remaining / 1000000),
                 static_cast<suseconds_t>(remaining % 1000000)};
    const int ready = select(fd + 1, nullptr, &writable, nullptr, deadline ? &wait : nullptr);
    if (ready == 0) return HTTPD_SOCK_ERR_TIMEOUT;
    if (ready < 0) {
      if (errno == EINTR) continue;
      return HTTPD_SOCK_ERR_FAIL;
    }
    const int result = send(fd, data + written, length - written, flags | MSG_DONTWAIT);
    if (result > 0) {
      written += static_cast<size_t>(result);
      continue;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
    return HTTPD_SOCK_ERR_FAIL;
  }
  return static_cast<int>(written);
}

void send_pending(void*) {
  if (!stopping.load(std::memory_order_acquire)) {
    for (auto& client : listeners) {
      if (client.fd < 0 || client.closing) continue;
      // A second packet lets one delayed HTTP cycle catch up instead of
      // permanently lowering the delivered sample rate.
      for (unsigned burst = 0; burst < 2; ++burst) {
        if (history_lock.test_and_set(std::memory_order_acquire)) break;
        if (invalidate_pending.exchange(false, std::memory_order_acq_rel)) history.reset();
        const auto result = history.read(client.cursor, pcm, kAudioPacketFrames);
        history_lock.clear(std::memory_order_release);
        if (!result.count) break;
        ring_read.fetch_add(result.count, std::memory_order_relaxed);
        dropped.fetch_add(result.dropped, std::memory_order_relaxed);
        const size_t size = encode_audio_packet(packet, sizeof(packet), result.generation,
            result.position, result.discontinuity, pcm, result.count);
        httpd_ws_frame_t frame{};
        frame.type = HTTPD_WS_TYPE_BINARY;
        frame.payload = packet;
        frame.len = size;
        client.send_deadline_us = esp_timer_get_time() + 100000;
        const esp_err_t send_result = httpd_ws_send_frame_async(server, client.fd, &frame);
        client.send_deadline_us = 0;
        if (send_result == ESP_OK) {
          sent.fetch_add(result.count, std::memory_order_relaxed);
          sent_frames.fetch_add(1, std::memory_order_relaxed);
          if (!result.remaining) break;
        } else {
          errors.fetch_add(1, std::memory_order_relaxed);
          client.closing = true;
          // Shutdown wakes httpd's session processing; its free_ctx owns cleanup.
          shutdown(client.fd, SHUT_RDWR);
          break;
        }
      }
    }
  }
  queued.store(false, std::memory_order_release);
}

void worker(void*) {
  TickType_t wake = xTaskGetTickCount();
  while (!stopping.load(std::memory_order_acquire)) {
    if (clients.load(std::memory_order_acquire)) {
      if (!queued.exchange(true) && httpd_queue_work(server, send_pending, nullptr) != ESP_OK) {
        queue_failures.fetch_add(1, std::memory_order_relaxed);
        queued.store(false, std::memory_order_release);
      }
    }
    xTaskDelayUntil(&wake, pdMS_TO_TICKS(20));
  }
  worker_alive.store(false, std::memory_order_release);
  vTaskDeleteWithCaps(nullptr);
}

esp_err_t handle_stream(httpd_req_t* req) {
  if (req->method != HTTP_GET) return ESP_FAIL; // Server-to-client stream only.
  char origin[128]{}, host[96]{};
  if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
      httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
      !origin_allowed(origin, host)) return ESP_FAIL;
  Client* available = nullptr;
  for (auto& client : listeners) if (client.fd < 0) { available = &client; break; }
  if (!available || stopping.load(std::memory_order_acquire)) return ESP_FAIL;
  if (history_lock.test_and_set(std::memory_order_acquire)) return ESP_FAIL;
  available->cursor = history.subscribe();
  history_lock.clear(std::memory_order_release);
  const int fd = httpd_req_to_sockfd(req);
  // Small WS headers must not wait for delayed ACKs before PCM is transmitted.
  const int no_delay = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)) != 0)
    return ESP_FAIL;
  if (httpd_sess_set_send_override(req->handle, fd, complete_send) != ESP_OK) return ESP_FAIL;
  available->fd = fd;
  available->closing = false;
  req->sess_ctx = available;
  req->free_ctx = release_client;
  clients.fetch_add(1, std::memory_order_release);
  return ESP_OK;
}
} // namespace

bool start(httpd_handle_t handle) {
  server = handle;
  queued.store(false);
  stopping.store(false);
  httpd_uri_t route{};
  route.uri = "/api/audio/stream";
  route.method = HTTP_GET;
  route.handler = handle_stream;
  route.is_websocket = true;
  if (httpd_register_uri_handler(server, &route) != ESP_OK) { stopping.store(true); return false; }
  worker_alive.store(true);
  if (xTaskCreatePinnedToCoreWithCaps(worker, "web_audio", 4096, nullptr, 2,
      nullptr, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    worker_alive.store(false);
    stopping.store(true);
    httpd_unregister_uri_handler(server, route.uri, route.method);
    return false;
  }
  return true;
}

void stop() {
  stopping.store(true, std::memory_order_release);
  // httpd remains alive until the scheduling task has stopped referencing it.
  while (worker_alive.load(std::memory_order_acquire)) vTaskDelay(1);
  invalidate();
}
bool demanded() { return !stopping.load(std::memory_order_relaxed) && clients.load(std::memory_order_acquire); }
void invalidate() { invalidate_pending.store(true, std::memory_order_release); }
void note_generated(size_t count) { producer.fetch_add(count, std::memory_order_relaxed); }
void publish(const int16_t* samples, size_t count) {
  if (!samples || !count) return;
  if (!demanded()) return;
  if (history_lock.test_and_set(std::memory_order_acquire)) {
    contention.fetch_add(count, std::memory_order_relaxed);
    return;
  }
  if (invalidate_pending.exchange(false, std::memory_order_acq_rel)) history.reset();
  history.append(samples, count);
  ring_written.fetch_add(count, std::memory_order_relaxed);
  history_lock.clear(std::memory_order_release);
}
Counters counters() {
  return {
      static_cast<uint64_t>(esp_timer_get_time()), producer.load(), ring_written.load(),
      ring_read.load(), sent.load(), sent_frames.load(), contention.load(), dropped.load(),
      clients.load(), queue_failures.load(), errors.load()};
}
} // namespace orcsdr::web_audio
