#include "sd_writer.hpp"

#include <cstring>

namespace orcsdr::sd_writer {

bool Writer::submit(const char* path, const void* header, size_t header_len, const void* data, size_t data_len) {
  if (state_.load(std::memory_order_acquire) != State::idle) return false;
  if (path == nullptr || header_len > kHeaderMax || (header_len > 0 && header == nullptr) ||
      (data_len > 0 && data == nullptr) || header_len + data_len == 0)
    return false;
  const size_t path_len = std::strlen(path);
  if (path_len == 0 || path_len >= kPathMax) return false;
  std::memcpy(path_, path, path_len + 1);
  if (header_len) std::memcpy(header_, header, header_len);
  header_len_ = header_len;
  data_ = static_cast<const uint8_t*>(data);
  data_len_ = data_len;
  failure_ = Failure::none;
  written_ = 0;
  state_.store(State::queued, std::memory_order_release);  // publishes the job to the worker
  if (platform_.wake) platform_.wake();
  return true;
}

Result Writer::result() const {
  Result r;
  r.state = state_.load(std::memory_order_acquire);
  if (r.state == State::done || r.state == State::failed) {
    r.failure = failure_;
    r.written = written_;
  }
  r.wanted = header_len_ + data_len_;
  return r;
}

bool Writer::busy() const {
  const State s = state_.load(std::memory_order_acquire);
  return s == State::queued || s == State::writing;
}

void Writer::acknowledge() {
  State expected = State::done;
  if (!state_.compare_exchange_strong(expected, State::idle, std::memory_order_acq_rel)) {
    expected = State::failed;
    (void)state_.compare_exchange_strong(expected, State::idle, std::memory_order_acq_rel);
  }
}

bool Writer::run_once() {
  State expected = State::queued;
  if (!state_.compare_exchange_strong(expected, State::writing, std::memory_order_acq_rel)) return false;

  void* file = platform_.open(path_);
  if (file == nullptr) {
    failure_ = Failure::open;
    state_.store(State::failed, std::memory_order_release);
    return true;
  }
  size_t written = 0;
  bool short_write = false;
  if (header_len_) {
    const size_t n = platform_.write(file, header_, header_len_);
    written += n;
    short_write = n != header_len_;
  }
  const size_t chunk = platform_.chunk_bytes ? platform_.chunk_bytes : data_len_;
  uint8_t* staging = nullptr;
  if (!short_write && data_len_ && platform_.alloc_staging)
    staging = static_cast<uint8_t*>(platform_.alloc_staging(chunk < data_len_ ? chunk : data_len_));
  for (size_t offset = 0; !short_write && offset < data_len_;) {
    const size_t n = data_len_ - offset < chunk ? data_len_ - offset : chunk;
    const uint8_t* source = data_ + offset;
    if (staging) {
      std::memcpy(staging, source, n);  // aligned for the card's DMA, whatever the caller's buffer
      source = staging;
    }
    const size_t accepted = platform_.write(file, source, n);
    written += accepted;
    offset += accepted;
    short_write = accepted != n;
    if (!short_write && offset < data_len_ && platform_.pause) platform_.pause();
  }
  if (staging && platform_.free_staging) platform_.free_staging(staging);
  (void)platform_.close(file);  // like the inline writers it replaces, a failed close is not reported separately
  written_ = written;
  failure_ = short_write ? Failure::write : Failure::none;
  state_.store(short_write ? State::failed : State::done, std::memory_order_release);
  return true;
}

}  // namespace orcsdr::sd_writer
