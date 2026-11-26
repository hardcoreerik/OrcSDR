#include "dv_event_log.hpp"

#include <cstdio>

namespace orcsdr::dv_event_log {

size_t format(const Entry& e, char* out, size_t cap) {
  if (!out || cap < kMaxLine) return 0;
  size_t w = 0;
  // A macro, not a helper function: every format string stays a literal the compiler can check.
#define PUT(...) do { const int n_ = std::snprintf(out + w, cap - w, __VA_ARGS__); if (n_ > 0 && static_cast<size_t>(n_) < cap - w) w += static_cast<size_t>(n_); } while (0)
  PUT("{");
  if (e.utc) PUT("\"utc\":%lu,", static_cast<unsigned long>(e.utc));
  PUT("\"ms\":%lu,\"hz\":%lu,\"p\":\"%s\",\"ev\":\"%s\"", static_cast<unsigned long>(e.ms), static_cast<unsigned long>(e.hz), e.protocol, e.evidence);
  if (e.has_cc) PUT(",\"cc\":%u", static_cast<unsigned>(e.cc));
  if (e.has_ran) PUT(",\"ran\":%u", static_cast<unsigned>(e.ran));
  if (e.has_rf_channel) PUT(",\"rf\":%u", static_cast<unsigned>(e.rf_channel));
  if (e.has_slot) PUT(",\"slot\":%u", static_cast<unsigned>(e.slot));
  if (e.has_data_type) PUT(",\"dt\":%u,\"flco\":%u,\"fid\":%u,\"svc\":%u", static_cast<unsigned>(e.data_type), static_cast<unsigned>(e.flco),
                           static_cast<unsigned>(e.feature_id), static_cast<unsigned>(e.service_options));
  if (e.has_ids) PUT(",\"src\":%lu,\"dst\":%lu,\"grp\":%d", static_cast<unsigned long>(e.source_id), static_cast<unsigned long>(e.destination_id), e.group ? 1 : 0);
  if (e.late_entry) PUT(",\"late\":1");
  if (e.info_valid) {
    PUT(",\"info\":\"");
    for (int i = 0; i < 12; ++i) PUT("%02x", static_cast<unsigned>(e.info[i]));
    PUT("\"");
  }
  PUT("}\n");
  return w;
}

void Log::begin(char* a, char* b, size_t capacity_each, uint32_t session) {
  buf_[0] = a; buf_[1] = b; cap_ = capacity_each; len_[0] = len_[1] = 0; peek_ = -1;
  first_ms_[0] = first_ms_[1] = 0; fill_ = 0; out_ = -1; saving_ = false; session_ = session; seq_ = 0;
  lines_ = dropped_ = saved_ = failed_ = bytes_saved_ = 0;
}

bool Log::add(const Entry& e, uint32_t now_ms) {
  if (!active()) return false;
  if (cap_ - len_[fill_] < kMaxLine) {                 // this buffer is full: use the other one if the writer is not holding it
    const int other = 1 - fill_;
    if (out_ == other || len_[other] != 0) { ++dropped_; return false; }
    fill_ = other;
  }
  const size_t n = format(e, buf_[fill_] + len_[fill_], cap_ - len_[fill_]);
  if (!n) { ++dropped_; return false; }
  if (len_[fill_] == 0) first_ms_[fill_] = now_ms ? now_ms : 1u;
  len_[fill_] += n;
  ++lines_;
  return true;
}

bool Log::ready(const char** data, size_t* length, char* name, size_t name_size, uint32_t now_ms, bool force) {
  if (!active() || saving_ || out_ >= 0) return false;
  int pick = -1;
  if (len_[fill_] && (force || len_[fill_] >= cap_ / 4 * 3 || static_cast<uint32_t>(now_ms - first_ms_[fill_]) >= 60000u)) pick = fill_;
  else if (len_[1 - fill_] && (len_[1 - fill_] >= cap_ / 4 * 3)) pick = 1 - fill_;   // the other one was left full while the writer was busy
  if (pick < 0) return false;
  peek_ = pick;
  if (data) *data = buf_[pick];
  if (length) *length = len_[pick];
  if (name && name_size) {
    if (session_) std::snprintf(name, name_size, "/orcsdr/dv_log_%lu_%04lu.jsonl", static_cast<unsigned long>(session_), static_cast<unsigned long>(seq_));
    else std::snprintf(name, name_size, "/orcsdr/dv_log_n%lu_%04lu.jsonl", static_cast<unsigned long>(now_ms), static_cast<unsigned long>(seq_));
  }
  return true;
}

void Log::saving() {
  if (peek_ < 0) return;
  out_ = peek_;
  if (out_ == fill_) fill_ = 1 - fill_;                 // new lines go to the other buffer while this one is written
  peek_ = -1; saving_ = true; ++seq_;
}

void Log::release(bool saved) {
  if (out_ < 0) { saving_ = false; return; }
  if (saved) { ++saved_; bytes_saved_ += static_cast<uint32_t>(len_[out_]); } else ++failed_;
  len_[out_] = 0; first_ms_[out_] = 0;
  out_ = -1; saving_ = false;
}

}  // namespace orcsdr::dv_event_log
