#pragma once
// Digital Voice data log (SETUP > LOG DATA, off by default): one JSON line per verified event, written to the SD card in numbered segments.
// Pure C++, no I/O: the app hands in two buffers and a clock, and pulls finished segments out for the shared background SD writer.
//
//   {"utc":1795500000,"ms":123456,"hz":463400000,"p":"dmr","ev":"identity","cc":1,"slot":1,"dt":1,"src":65535,"dst":16,"grp":1,"info":"0004...."}
//
// Nothing is guessed: a field is written only when the decoder reported it as valid. "info" is the 96 information bits (12 bytes, hex) of a
// data burst as BPTC-decoded, so a data message can be decoded offline later. Third-party call details stay on the owner's card.
#include <cstddef>
#include <cstdint>

namespace orcsdr::dv_event_log {

struct Entry {
  uint32_t utc = 0;                 // receiver clock, 0 = unknown (the field is left out)
  uint32_t ms = 0, hz = 0;
  const char* protocol = "";        // "dmr", "nxdn48", "nxdn96"
  const char* evidence = "";        // "integrity" or "identity"
  bool has_cc = false, has_slot = false, has_data_type = false, has_ids = false, group = false, late_entry = false;
  bool has_ran = false, has_rf_channel = false, info_valid = false;
  uint8_t cc = 0, slot = 0, data_type = 0, flco = 0, feature_id = 0, service_options = 0, ran = 0, rf_channel = 0;
  uint32_t source_id = 0, destination_id = 0;
  uint8_t info[12] = {};
};

// One line including the trailing newline. Returns the bytes written, or 0 when `capacity` is too small.
size_t format(const Entry& e, char* out, size_t capacity);

constexpr size_t kMaxLine = 320;

class Log {
 public:
  // Two equal buffers (the writer reads one while the other fills). `session` names the files: the receiver's UTC at the start, or 0.
  void begin(char* a, char* b, size_t capacity_each, uint32_t session);
  bool active() const { return buf_[0] != nullptr; }
  // Appends one line. False (and counted) when both buffers are full.
  bool add(const Entry& e, uint32_t now_ms);
  // A finished segment for the SD writer: when the filling buffer is three quarters full or 60 s old (or `force`) and the writer is idle. This only
  // looks; nothing changes until saving() says the writer took it. The data stays valid, and unchanged, until release().
  bool ready(const char** data, size_t* length, char* name, size_t name_size, uint32_t now_ms, bool force = false);
  void saving();                     // the segment offered by the last ready() was accepted by the writer
  void release(bool saved);          // the writer finished it (saved) or failed

  uint32_t session() const { return session_; }
  uint32_t lines() const { return lines_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t segments_saved() const { return saved_; }
  uint32_t segments_failed() const { return failed_; }
  uint32_t bytes_saved() const { return bytes_saved_; }

 private:
  char* buf_[2] = {nullptr, nullptr};
  size_t cap_ = 0, len_[2] = {0, 0};
  uint32_t first_ms_[2] = {0, 0};
  int fill_ = 0;                     // buffer lines are appended to
  int out_ = -1;                     // buffer handed to the writer, -1 = none
  int peek_ = -1;                    // buffer offered by the last ready()
  bool saving_ = false;
  uint32_t session_ = 0, seq_ = 0;
  uint32_t lines_ = 0, dropped_ = 0, saved_ = 0, failed_ = 0, bytes_saved_ = 0;
};

}  // namespace orcsdr::dv_event_log
