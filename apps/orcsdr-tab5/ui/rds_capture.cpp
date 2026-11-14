#include "rds_capture.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "buffer_claim.hpp"
#include "memory_reclaim.hpp"

namespace orcsdr::rds_capture {
namespace {

int16_t* g_buf = nullptr;
std::atomic<size_t> g_write{0};
std::atomic<bool> g_active{false};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_writing{false};  // the DSP task is inside the buffer
std::atomic<bool> g_saving{false};   // a save is queued or being written: the buffer must stay
uint32_t g_frequency_hz = 0;
uint32_t g_started_ms = 0;
uint32_t g_file_seq = 0;
char g_last_path[96] = "";
buffer_claim::Claim g_claim;         // held to arm, stop or free the buffer
const Hooks* g_hooks = nullptr;      // for the reclaim holder, which takes no arguments

// The save, driven from the main loop.
enum class Export : uint8_t { none, raw_waiting, raw, json_waiting, json };
Export g_export = Export::none;
char g_raw_path[96] = "";
char g_json_path[96] = "";
char g_json[sd_writer::kHeaderMax] = "";
size_t g_json_len = 0;
size_t g_export_samples = 0;

void say(const Hooks& hooks, const char* format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  hooks.write(line);
}

void copy_path(char* to, size_t size, const char* from) {
  std::snprintf(to, size, "%s", from);
}

buffer_claim::CaptureState state() {
  buffer_claim::CaptureState s;
  s.allocated = g_buf != nullptr;
  s.active = g_active.load();
  s.writing = g_writing.load();
  s.pending = g_saving.load();
  s.samples = g_write.load();
  s.saved = g_last_path[0] != '\0';
  return s;
}

// Hands the next piece of the save to the writer if it is free (another feature may be using it).
void try_submit(const Hooks& hooks) {
  sd_writer::Writer& writer = hooks.writer();
  if (g_export == Export::raw_waiting &&
      writer.submit(g_raw_path, nullptr, 0, g_buf, g_export_samples * sizeof(int16_t)))
    g_export = Export::raw;
  else if (g_export == Export::json_waiting && writer.submit(g_json_path, g_json, g_json_len, nullptr, 0))
    g_export = Export::json;
}

// Same names, folders and note as the inline writer it replaces. Returns false (silently, as before) when there is
// nothing to save or no card; the caller then prints RTL_RDS_CAPTURE_PSRAM_HOLD.
bool begin_export(const Hooks& hooks, size_t samples) {
  if (g_buf == nullptr || samples == 0 || !hooks.sd_ready()) return false;
  (void)hooks.mkdir("/orcsdr");
  (void)hooks.mkdir("/orcsdr/rds_debug");
  do {
    ++g_file_seq;
    std::snprintf(g_raw_path, sizeof(g_raw_path), "/orcsdr/rds_debug/%03u_%lu_mpx.s16",
                  static_cast<unsigned>(g_file_seq), static_cast<unsigned long>(g_frequency_hz));
  } while (hooks.exists(g_raw_path));
  std::snprintf(g_json_path, sizeof(g_json_path), "/orcsdr/rds_debug/%03u_%lu_mpx.json",
                static_cast<unsigned>(g_file_seq), static_cast<unsigned long>(g_frequency_hz));
  const int json_len = std::snprintf(
      g_json, sizeof(g_json),
      "{\"format\":\"s16le_mpx\",\"sample_rate_hz\":%lu,\"frequency_hz\":%lu,"
      "\"samples\":%u,\"scale_radians_per_lsb\":%.10g,\"start_uptime_ms\":%lu}\n",
      static_cast<unsigned long>(kRateHz), static_cast<unsigned long>(g_frequency_hz),
      static_cast<unsigned>(samples), hooks.scale_radians_per_lsb, static_cast<unsigned long>(g_started_ms));
  g_json_len = json_len < 0 ? 0 : (static_cast<size_t>(json_len) < sizeof(g_json) ? static_cast<size_t>(json_len)
                                                                                   : sizeof(g_json) - 1);
  g_export_samples = samples;
  g_saving.store(true);  // before the claim is given back: a reclaim must now leave the buffer alone
  g_export = Export::raw_waiting;
  try_submit(hooks);
  return true;
}

void end_export_held(const Hooks& hooks) {
  say(hooks, "RTL_RDS_CAPTURE_PSRAM_HOLD samples=%u note=insert_sd_then_stop_again\n",
      static_cast<unsigned>(g_export_samples));
  g_export = Export::none;
  g_saving.store(false);
}

}  // namespace

bool start(const Hooks& hooks) {
  if (!hooks.fm_selected()) {
    hooks.write_line("RTL_RDS_CAPTURE_ERROR fm_mode_required");
    return false;
  }
  if (g_active.load(std::memory_order_acquire)) {
    hooks.write_line("RTL_RDS_CAPTURE_ALREADY");
    return true;
  }
  if (g_saving.load()) {  // the previous capture is still being written to the card
    hooks.write_line("RTL_RDS_CAPTURE_ERROR saving");
    return false;
  }
  g_hooks = &hooks;
  (void)memory_reclaim::register_holder({"RDS capture", held_bytes, release_buffer});
  g_claim.take(hooks.wait);
  buffer_claim::Hold hold(g_claim);
  if (g_buf == nullptr) g_buf = hooks.alloc(kBufferBytes);
  if (g_buf == nullptr) {
    hooks.write_line("RTL_RDS_CAPTURE_ERROR no_psram_buffer");
    return false;
  }
  g_frequency_hz = hooks.frequency_hz();
  g_started_ms = hooks.now_ms();
  g_last_path[0] = '\0';
  g_write.store(0, std::memory_order_release);
  g_ready.store(false, std::memory_order_release);
  g_active.store(true, std::memory_order_release);
  say(hooks, "RTL_RDS_CAPTURE_START samples=%u seconds=%u rate=%u frequency_hz=%u\n",
      static_cast<unsigned>(kSamples), static_cast<unsigned>(kSeconds), static_cast<unsigned>(kRateHz),
      static_cast<unsigned>(g_frequency_hz));
  return true;
}

bool stop_and_export(const Hooks& hooks) {
  if (g_saving.load()) {  // a second STOP while the first save is still being written
    hooks.write_line("RTL_RDS_CAPTURE_STOP busy=saving");
    return true;
  }
  g_claim.take(hooks.wait);
  buffer_claim::Hold hold(g_claim);
  g_active.store(false, std::memory_order_release);
  while (g_writing.load(std::memory_order_acquire)) hooks.wait();
  const size_t samples = g_write.load(std::memory_order_acquire);
  if (samples == 0) {
    hooks.write_line("RTL_RDS_CAPTURE_STOP empty");
    return false;
  }
  g_ready.store(true, std::memory_order_release);
  say(hooks, "RTL_RDS_CAPTURE_STOP samples=%u seconds=%.3f\n", static_cast<unsigned>(samples),
      static_cast<double>(samples) / static_cast<double>(kRateHz));
  if (begin_export(hooks, samples)) return true;  // RTL_RDS_CAPTURE_SAVED follows from service()
  g_export_samples = samples;
  end_export_held(hooks);
  return false;
}

void service(const Hooks& hooks) {
  if (!g_saving.load() || g_export == Export::none) return;
  if (g_export == Export::raw_waiting || g_export == Export::json_waiting) {
    try_submit(hooks);
    return;
  }
  sd_writer::Writer& writer = hooks.writer();
  const sd_writer::Result result = writer.result();
  if (result.state != sd_writer::State::done && result.state != sd_writer::State::failed) return;
  writer.acknowledge();
  if (g_export == Export::raw) {
    const size_t bytes = g_export_samples * sizeof(int16_t);
    if (result.failure == sd_writer::Failure::open) {
      hooks.write_line("RTL_RDS_CAPTURE_ERROR raw_open_failed");
      end_export_held(hooks);
      return;
    }
    if (result.written != bytes) {
      (void)hooks.remove(g_raw_path);
      say(hooks, "RTL_RDS_CAPTURE_ERROR short_write got=%u want=%u\n", static_cast<unsigned>(result.written),
          static_cast<unsigned>(bytes));
      end_export_held(hooks);
      return;
    }
    g_export = Export::json_waiting;
    try_submit(hooks);
    return;
  }
  // The note. As before, only a note that cannot be created counts as a failure.
  if (result.failure == sd_writer::Failure::open) {
    say(hooks, "RTL_RDS_CAPTURE_ERROR metadata_open_failed raw=\"%s\"\n", g_raw_path);
    end_export_held(hooks);
    return;
  }
  copy_path(g_last_path, sizeof(g_last_path), g_raw_path);
  say(hooks, "RTL_RDS_CAPTURE_SAVED path=\"%s\" metadata=\"%s\" samples=%u rate=%u\n", g_raw_path, g_json_path,
      static_cast<unsigned>(g_export_samples), static_cast<unsigned>(kRateHz));
  g_export = Export::none;
  g_saving.store(false);
}

void status_print(const Hooks& hooks) {
  const size_t samples = g_write.load(std::memory_order_acquire);
  say(hooks,
      "RTL_RDS_CAPTURE_STATUS active=%s ready=%s samples=%u seconds=%.3f max_seconds=%u "
      "rate=%u frequency_hz=%u last_path=\"%s\" sd=%s\n",
      g_active.load(std::memory_order_acquire) ? "true" : "false",
      g_ready.load(std::memory_order_acquire) ? "true" : "false", static_cast<unsigned>(samples),
      static_cast<double>(samples) / static_cast<double>(kRateHz), static_cast<unsigned>(kSeconds),
      static_cast<unsigned>(kRateHz), static_cast<unsigned>(g_frequency_hz),
      g_last_path[0] ? g_last_path : "none", hooks.sd_state());
}

bool tap_begin(Tap* tap) {
  if (!g_active.load(std::memory_order_acquire)) return false;
  // Sequentially consistent: a stop or release on another task must see this flag before this task re-reads `active`.
  g_writing.store(true, std::memory_order_seq_cst);
  if (!g_active.load(std::memory_order_seq_cst)) {
    g_writing.store(false, std::memory_order_release);
    return false;
  }
  tap->data = g_buf;
  tap->write = g_write.load(std::memory_order_relaxed);
  tap->capacity = kSamples;
  return true;
}

void tap_end(const Tap& tap, const Hooks& hooks) {
  g_write.store(tap.write, std::memory_order_release);
  if (tap.write >= kSamples) {
    g_active.store(false, std::memory_order_release);
    g_ready.store(true, std::memory_order_release);
    say(hooks, "RTL_RDS_CAPTURE_FULL samples=%u seconds=%u note=send_stop_to_export\n",
        static_cast<unsigned>(tap.write), static_cast<unsigned>(kSeconds));
  }
  g_writing.store(false, std::memory_order_release);
}

const void* buffer() { return g_buf; }

bool saving() { return g_saving.load(); }

size_t held_bytes() { return buffer_claim::releasable(state()) ? kBufferBytes : 0u; }

size_t release_buffer() {
  if (!g_claim.try_take()) return 0;  // being armed or stopped right now
  buffer_claim::Hold hold(g_claim);
  if (!buffer_claim::releasable(state()) || g_hooks == nullptr) return 0;
  int16_t* buffer = g_buf;
  g_buf = nullptr;     // clear first: every user checks for null and the next capture allocates it again
  g_write.store(0);    // the samples are gone; the saved file's path stays in the status line
  g_ready.store(false);
  g_hooks->release(buffer);
  return kBufferBytes;
}

void reset_for_tests() {
  g_buf = nullptr;
  g_write.store(0);
  g_active.store(false);
  g_ready.store(false);
  g_writing.store(false);
  g_saving.store(false);
  g_frequency_hz = 0;
  g_started_ms = 0;
  g_file_seq = 0;
  g_last_path[0] = '\0';
  g_hooks = nullptr;
  g_export = Export::none;
  g_export_samples = 0;
  if (g_claim.held()) g_claim.give();
}

}  // namespace orcsdr::rds_capture
