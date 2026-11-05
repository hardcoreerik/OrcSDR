#include "rtl_tcp_server_session.hpp"

#include <cerrno>
#include <cstring>
#include <memory>
#include <new>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace orcsdr::rtl_tcp {
namespace {

constexpr size_t kIqChunkBytes = 16384;
constexpr uint32_t kGreetingSendMs = 2000;

bool would_block(int error) { return error == EAGAIN || error == EWOULDBLOCK; }

bool known_command(uint8_t id) { return id >= 0x01 && id <= 0x0e; }

void set_nonblocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

// Small blocking-ish send for the greeting: the client has only just connected.
bool send_greeting(int fd, const uint8_t* data, size_t length, ServerBackend& backend) {
  size_t sent = 0;
  const uint32_t started = backend.now_ms();
  while (sent < length) {
    const ssize_t n = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
    if (n > 0) {
      sent += static_cast<size_t>(n);
    } else if (n < 0 && would_block(errno)) {
      if (backend.now_ms() - started > kGreetingSendMs || backend.stop_requested()) return false;
      backend.sleep_ms(2);
    } else {
      return false;
    }
  }
  return true;
}

void refuse_extra(int listen_fd, ServerStats& stats) {
  const int extra = accept(listen_fd, nullptr, nullptr);
  if (extra >= 0) {
    close(extra);
    stats.clients_refused.fetch_add(1, std::memory_order_relaxed);
  }
}

// Serves one client until it leaves, stalls, or the server is stopped.
void serve_client(int fd, int listen_fd, const ServerConfig& config, ServerBackend& backend,
                  ServerStats& stats, uint8_t* iq) {  // iq: kIqChunkBytes, off the task stack
  set_nonblocking(fd);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  if (!backend.client_connected()) {
    stats.clients_refused.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  stats.clients_accepted.fetch_add(1, std::memory_order_relaxed);

  uint8_t hello[kGreetingBytes];
  const Greeting g = backend.greeting();
  encode_greeting(g.tuner_type_raw, g.gain_count, hello);

  if (send_greeting(fd, hello, sizeof(hello), backend)) {
    CommandParser parser;
    size_t iq_length = 0, iq_offset = 0;
    uint32_t last_progress = backend.now_ms();
    bool alive = true;
    while (alive && !backend.stop_requested()) {
      if (!backend.session_alive()) break;

      // Keep the pipe full: send for as long as the socket takes data, and only
      // wait when it cannot. (A wait per chunk costs a whole RTOS tick each time.)
      bool sent_any = false;
      for (int burst = 0; burst < 8 && alive; ++burst) {
        if (iq_offset >= iq_length) {
          iq_length = backend.read_iq(iq, kIqChunkBytes);
          iq_offset = 0;
          if (iq_length == 0) {
            last_progress = backend.now_ms();  // nothing to send is not a stall
            break;
          }
        }
        const ssize_t n = send(fd, iq + iq_offset, iq_length - iq_offset, MSG_NOSIGNAL);
        if (n > 0) {
          iq_offset += static_cast<size_t>(n);
          stats.bytes_sent.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
          last_progress = backend.now_ms();
          sent_any = true;
        } else if (n < 0 && !would_block(errno)) {
          stats.last_errno.store(errno, std::memory_order_relaxed);
          alive = false;
        } else {
          if (backend.now_ms() - last_progress >= config.send_stall_ms) {
            stats.stalled_clients.fetch_add(1, std::memory_order_relaxed);
            alive = false;  // not reading; do not hold the receiver for it
          }
          break;
        }
      }
      if (!alive) break;

      // Commands and extra connections. Wait only when there was nothing to send.
      fd_set readable, writable;
      FD_ZERO(&readable);
      FD_ZERO(&writable);
      FD_SET(fd, &readable);
      FD_SET(listen_fd, &readable);
      const bool pending = iq_offset < iq_length;
      if (pending) FD_SET(fd, &writable);
      const int nfds = (fd > listen_fd ? fd : listen_fd) + 1;
      timeval wait{0, sent_any ? 0 : (pending ? 10 : 2) * 1000};
      select(nfds, &readable, pending ? &writable : nullptr, nullptr, &wait);

      if (FD_ISSET(listen_fd, &readable)) refuse_extra(listen_fd, stats);

      if (FD_ISSET(fd, &readable)) {
        uint8_t buffer[256];
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
          for (ssize_t i = 0; i < n; ++i) {
            RawCommand command;
            if (!parser.push(buffer[i], &command)) continue;
            if (known_command(command.id)) {
              backend.apply(command);
              stats.commands_applied.fetch_add(1, std::memory_order_relaxed);
            } else {
              stats.commands_unknown.fetch_add(1, std::memory_order_relaxed);
            }
          }
        } else if (n == 0 || !would_block(errno)) {
          alive = false;  // the client left
          break;
        }
      }
    }
  }
  backend.client_disconnected();
}

}  // namespace

const char* server_state_name(ServerState state) {
  switch (state) {
    case ServerState::listening: return "listening";
    case ServerState::serving: return "serving";
    case ServerState::failed: return "failed";
    default: return "stopped";
  }
}

ServerState run_server(const ServerConfig& config, ServerBackend& backend, ServerStats& stats) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(config.port);
  if (!valid_host(config.bind_host) ||
      inet_pton(AF_INET, config.bind_host, &address.sin_addr) != 1) {
    backend.on_state(ServerState::failed);  // an explicit IPv4 bind address is required
    return ServerState::failed;
  }
  const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    stats.last_errno.store(errno, std::memory_order_relaxed);
    backend.on_state(ServerState::failed);
    return ServerState::failed;
  }
  int one = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(listen_fd, 2) != 0) {
    stats.last_errno.store(errno, std::memory_order_relaxed);
    close(listen_fd);
    backend.on_state(ServerState::failed);
    return ServerState::failed;
  }
  set_nonblocking(listen_fd);
  std::unique_ptr<uint8_t[]> iq(new (std::nothrow) uint8_t[kIqChunkBytes]);
  if (!iq) {
    close(listen_fd);
    backend.on_state(ServerState::failed);
    return ServerState::failed;
  }
  backend.on_state(ServerState::listening);

  while (!backend.stop_requested()) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listen_fd, &readable);
    timeval wait{0, 100 * 1000};
    if (select(listen_fd + 1, &readable, nullptr, nullptr, &wait) <= 0) continue;
    stats.select_ready.fetch_add(1, std::memory_order_relaxed);
    const int fd = accept(listen_fd, nullptr, nullptr);
    if (fd < 0) {
      stats.accept_failures.fetch_add(1, std::memory_order_relaxed);
      stats.last_errno.store(errno, std::memory_order_relaxed);
      continue;
    }
    backend.on_state(ServerState::serving);
    serve_client(fd, listen_fd, config, backend, stats, iq.get());
    close(fd);
    backend.on_state(ServerState::listening);
  }
  close(listen_fd);
  backend.on_state(ServerState::stopped);
  return ServerState::stopped;
}

}  // namespace orcsdr::rtl_tcp
