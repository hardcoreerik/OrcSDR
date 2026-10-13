#include "sstv_archive.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "orcsdr_storage.hpp"
#endif

namespace orcsdr::sstv_archive {

const char kIndexHeaderV2[] =
    "session_id,utc_epoch,utc_verified,frequency_hz,mode,vis_code,identification,id_score,"
    "width,height,decoded_rows,complete,start_unknown,sync_locked,sync_missed,end_reason,"
    "content_hash,filename";

namespace {

using orcsdr::sstv::EndReason;
using orcsdr::sstv::Identification;
using orcsdr::sstv::Image;

// ---------------------------------------------------------------- default I/O
bool d_mounted() {
#ifdef ESP_PLATFORM
  return orcsdr::storage::mounted();
#else
  return true;
#endif
}
bool d_make_dir(const char* path) {
  if (::mkdir(path, 0775) == 0) return true;
  if (errno != EEXIST) {
    struct stat st{};
    return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
  }
  return true;
}
std::FILE* d_open(const char* path, const char* mode) { return std::fopen(path, mode); }
size_t d_write(const void* data, size_t size, std::FILE* file) { return std::fwrite(data, 1, size, file); }
size_t d_read(void* data, size_t size, std::FILE* file) { return std::fread(data, 1, size, file); }
int d_close(std::FILE* file) { return std::fclose(file); }
int d_rename(const char* from, const char* to) { return std::rename(from, to); }
int d_remove(const char* path) { return std::remove(path); }
bool d_exists(const char* path) {
  struct stat st{};
  return ::stat(path, &st) == 0;
}
uint64_t d_free_bytes() {
#ifdef ESP_PLATFORM
  const uint64_t total = orcsdr::storage::total_bytes();
  const uint64_t used = orcsdr::storage::used_bytes();
  return total > used ? total - used : 0;
#else
  return ~static_cast<uint64_t>(0);
#endif
}
bool d_list_dir(const char* dir, void (*visit)(const char*, void*), void* ctx) {
  DIR* handle = ::opendir(dir);
  if (!handle) return false;
  while (const dirent* entry = ::readdir(handle)) visit(entry->d_name, ctx);
  ::closedir(handle);
  return true;
}
uint32_t d_now_ms() {
#ifdef ESP_PLATFORM
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
#else
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}
void d_yield() {
#ifdef ESP_PLATFORM
  vTaskDelay(1);
#else
  std::this_thread::yield();
#endif
}

// ---------------------------------------------------------------- state
constexpr size_t kQueue = 3;
constexpr size_t kResults = 8;
constexpr uint64_t kReserveBytes = 65536;  // never fill the card to the last block

struct Job {
  bool used = false;
  bool busy = false;
  Image image{};
  uint32_t utc = 0;
  uint8_t attempts = 0;
  uint32_t not_before = 0;
};

std::mutex g_mutex;
Job g_jobs[kQueue];
Result g_results[kResults];   // g_results[0] is the newest
size_t g_result_count = 0;
Stats g_stats;
Config g_config;
const Io* g_io = nullptr;
uint32_t g_last_serial = 0;
bool g_index_ready = false;
std::atomic<bool> g_run{false};
std::atomic<bool> g_worker_alive{false};
std::atomic<bool> g_recover_requested{false};
#ifndef ESP_PLATFORM
std::thread g_thread;
#endif

const Io& io() { return *g_io; }

const char* ident_name(Identification id) {
  switch (id) {
    case Identification::vis: return "vis";
    case Identification::sync: return "sync";
    case Identification::manual: return "manual";
    default: return "none";
  }
}
const char* end_name(EndReason reason) {
  switch (reason) {
    case EndReason::complete: return "complete";
    case EndReason::sync_lost: return "sync_lost";
    case EndReason::new_vis: return "new_vis";
    case EndReason::retune: return "retune";
    case EndReason::aborted: return "aborted";
    case EndReason::captured: return "captured";
    default: return "none";
  }
}

void note_result_locked(uint32_t session, JobState state, Error error, uint32_t duration,
                        uint32_t bytes, const char* filename) {
  size_t slot = g_result_count;
  for (size_t i = 0; i < g_result_count; ++i)
    if (g_results[i].session_id == session) { slot = i; break; }
  if (slot == g_result_count) {            // new entry: shift older ones down, drop the oldest
    const size_t keep = std::min(g_result_count, kResults - 1);
    for (size_t i = keep; i > 0; --i) g_results[i] = g_results[i - 1];
    g_result_count = std::min(kResults, keep + 1);
    slot = 0;
    g_results[0] = Result{};
  }
  Result& r = g_results[slot];
  r.session_id = session;
  r.state = state;
  r.error = error;
  r.duration_ms = duration;
  r.bytes = bytes;
  if (filename) std::snprintf(r.filename, sizeof(r.filename), "%s", filename);
}

// ---------------------------------------------------------------- files
bool make_dirs(const char* path) {
  char partial[96];
  std::snprintf(partial, sizeof(partial), "%s", path);
  for (char* p = partial + 1; *p; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    (void)io().make_dir(partial);          // "/sd" and similar may already be a mount point
    *p = '/';
  }
  return io().make_dir(partial);
}

void index_path(char* out, size_t cap) { std::snprintf(out, cap, "%s/index.csv", g_config.root); }

// Creates index.csv with the v2 header, first moving aside an index written by the
// first receiver (v1 had no header version and fewer columns).
bool ensure_index() {
  if (g_index_ready) return true;
  char path[96];
  index_path(path, sizeof(path));
  if (io().exists(path)) {
    char head[sizeof(kIndexHeaderV2)] = {};
    bool current = false;
    if (std::FILE* f = io().open(path, "rb")) {
      const size_t n = io().read(head, sizeof(kIndexHeaderV2) - 1, f);
      io().close(f);
      current = n >= sizeof(kIndexHeaderV2) - 1 &&
                std::strncmp(head, kIndexHeaderV2, sizeof(kIndexHeaderV2) - 1) == 0;
    }
    if (current) { g_index_ready = true; return true; }
    char moved[112];
    std::snprintf(moved, sizeof(moved), "%s/index-v1-%lu.csv", g_config.root,
                  static_cast<unsigned long>(io().now_ms()));
    if (io().rename(path, moved) != 0) return false;
  }
  std::FILE* f = io().open(path, "wb");
  if (!f) return false;
  char line[sizeof(kIndexHeaderV2) + 2];
  const int n = std::snprintf(line, sizeof(line), "%s\n", kIndexHeaderV2);
  const bool ok = io().write(line, static_cast<size_t>(n), f) == static_cast<size_t>(n);
  if (io().close(f) != 0 || !ok) { io().remove(path); return false; }
  g_index_ready = true;
  return true;
}

struct Row {
  uint32_t session = 0, utc = 0, frequency = 0;
  bool utc_verified = false;
  const char* mode = "UNKNOWN";
  uint8_t vis = 0;
  const char* identification = "none";
  uint8_t score = 0;
  uint16_t width = 0, height = 0, rows = 0;
  bool complete = false, start_unknown = false;
  uint16_t locked = 0, missed = 0;
  const char* end = "none";
  uint64_t hash = 0;
  const char* file = "";
};

bool append_index(const Row& r) {
  char path[96];
  index_path(path, sizeof(path));
  std::FILE* f = io().open(path, "ab");
  if (!f) return false;
  char line[320];
  const int n = std::snprintf(
      line, sizeof(line), "%lu,%lu,%u,%lu,%s,%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%s,%016llx,%s\n",
      static_cast<unsigned long>(r.session), static_cast<unsigned long>(r.utc),
      r.utc_verified ? 1u : 0u, static_cast<unsigned long>(r.frequency), r.mode,
      static_cast<unsigned>(r.vis), r.identification, static_cast<unsigned>(r.score),
      static_cast<unsigned>(r.width), static_cast<unsigned>(r.height), static_cast<unsigned>(r.rows),
      r.complete ? 1u : 0u, r.start_unknown ? 1u : 0u, static_cast<unsigned>(r.locked),
      static_cast<unsigned>(r.missed), r.end, static_cast<unsigned long long>(r.hash), r.file);
  const bool ok = n > 0 && io().write(line, static_cast<size_t>(n), f) == static_cast<size_t>(n);
  return io().close(f) == 0 && ok;
}

void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void put32(uint8_t* p, uint32_t v) { put16(p, static_cast<uint16_t>(v)); put16(p + 2, static_cast<uint16_t>(v >> 16)); }

// Writes a 24-bit bottom-up BMP in whole rows. Returns Error::none on success.
Error write_bmp(const Image& im, const char* path, uint64_t* hash, uint32_t* bytes) {
  std::FILE* f = io().open(path, "wb");
  if (!f) return Error::write_error;
  const uint32_t stride = (static_cast<uint32_t>(im.width) * 3u + 3u) & ~3u;
  const uint32_t data = stride * im.height;
  uint8_t header[54] = {};
  header[0] = 'B'; header[1] = 'M';
  put32(header + 2, data + 54);
  put32(header + 10, 54);
  put32(header + 14, 40);
  put32(header + 18, im.width);
  put32(header + 22, im.height);
  put16(header + 26, 1);
  put16(header + 28, 24);
  put32(header + 34, data);
  put32(header + 38, 2835);
  put32(header + 42, 2835);
  bool ok = io().write(header, sizeof(header), f) == sizeof(header);
  uint8_t row[800 * 3 + 4];
  uint64_t h = 1469598103934665603ull;  // FNV-1a over the RGB565 words
  for (int y = im.height - 1; ok && y >= 0; --y) {
    const uint16_t* src = im.pixels + static_cast<size_t>(y) * im.width;
    for (uint16_t x = 0; x < im.width; ++x) {
      const uint16_t c = src[x];
      row[x * 3u + 0] = static_cast<uint8_t>((c & 31u) << 3);
      row[x * 3u + 1] = static_cast<uint8_t>(((c >> 5) & 63u) << 2);
      row[x * 3u + 2] = static_cast<uint8_t>(((c >> 11) & 31u) << 3);
      h = (h ^ (c & 0xffu)) * 1099511628211ull;
      h = (h ^ (c >> 8)) * 1099511628211ull;
    }
    for (uint32_t k = im.width * 3u; k < stride; ++k) row[k] = 0;
    ok = io().write(row, stride, f) == stride;
    if ((y & 15) == 0 && io().yield) io().yield();   // let the decoder and USB tasks run
  }
  const bool closed = io().close(f) == 0;
  *hash = h;
  *bytes = data + 54;
  return ok && closed ? Error::none : Error::write_error;
}

enum class Outcome : uint8_t { saved, failed, retry };

Outcome process(const Job& job, Result* result) {
  const Image& im = job.image;
  result->session_id = im.session_id;
  if (!io().mounted()) { result->error = Error::no_card; return Outcome::retry; }
  if (!make_dirs(g_config.root) || !ensure_index()) {
    g_index_ready = false;
    result->error = io().mounted() ? Error::index_error : Error::no_card;
    return io().mounted() ? Outcome::failed : Outcome::retry;
  }
  const uint32_t stride = (static_cast<uint32_t>(im.width) * 3u + 3u) & ~3u;
  const uint64_t need = static_cast<uint64_t>(stride) * im.height + 54 + 512 + kReserveBytes;
  if (io().free_bytes() < need) { result->error = Error::no_space; return Outcome::failed; }

  char name[64], final_path[160], part_path[170];
  for (unsigned n = 0; n < 100; ++n) {
    if (n == 0)
      std::snprintf(name, sizeof(name), "rx_%lu_%lu_%lu.bmp", static_cast<unsigned long>(job.utc),
                    static_cast<unsigned long>(im.frequency_hz), static_cast<unsigned long>(im.session_id));
    else
      std::snprintf(name, sizeof(name), "rx_%lu_%lu_%lu-%u.bmp", static_cast<unsigned long>(job.utc),
                    static_cast<unsigned long>(im.frequency_hz), static_cast<unsigned long>(im.session_id), n);
    std::snprintf(final_path, sizeof(final_path), "%s/%s", g_config.root, name);
    if (!io().exists(final_path)) break;
  }
  std::snprintf(part_path, sizeof(part_path), "%s.part", final_path);
  std::snprintf(result->filename, sizeof(result->filename), "%s", name);

  uint64_t hash = 0;
  uint32_t bytes = 0;
  if (write_bmp(im, part_path, &hash, &bytes) != Error::none) {
    (void)io().remove(part_path);
    result->error = io().mounted() ? Error::write_error : Error::no_card;
    return io().mounted() ? Outcome::failed : Outcome::retry;
  }
  if (io().rename(part_path, final_path) != 0) {
    (void)io().remove(part_path);
    result->error = Error::rename_error;
    return Outcome::failed;
  }
  result->bytes = bytes;

  Row row;
  row.session = im.session_id;
  row.utc = job.utc;
  row.utc_verified = job.utc != 0;
  row.frequency = im.frequency_hz;
  row.mode = orcsdr::sstv::mode_name(im.mode);
  row.vis = im.vis_code;
  row.identification = ident_name(im.identification);
  row.score = im.identification_score;
  row.width = im.width;
  row.height = im.height;
  row.rows = im.decoded_rows;
  row.complete = !im.partial && im.decoded_rows == im.height;
  row.start_unknown = im.start_unknown;
  row.locked = im.sync_locked;
  row.missed = im.sync_missed;
  row.end = end_name(im.end_reason);
  row.hash = hash;
  row.file = name;
  if (!append_index(row)) {
    g_index_ready = false;                 // BMP stays on the card; recover() will index it
    result->error = Error::index_error;
    return Outcome::failed;
  }
  result->error = Error::none;
  return Outcome::saved;
}

bool any_job_locked() {
  for (const auto& j : g_jobs)
    if (j.used) return true;
  return false;
}
// A job the worker can start right now (not busy, retry delay elapsed).
bool ready_job_locked(uint32_t now) {
  for (const auto& j : g_jobs)
    if (j.used && !j.busy && static_cast<int32_t>(now - j.not_before) >= 0) return true;
  return false;
}

void abandon_all(Error error) {
  Job drop[kQueue];
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < kQueue; ++i) {
      if (!g_jobs[i].used || g_jobs[i].busy) continue;
      drop[i] = g_jobs[i];
      g_jobs[i] = Job{};
      note_result_locked(drop[i].image.session_id, JobState::failed, error, 0, 0, nullptr);
      ++g_stats.failed;
      if (g_stats.pending) --g_stats.pending;
      g_stats.last_error = error;
    }
  }
  for (const auto& j : drop)
    if (j.used) orcsdr::sstv::release_image(j.image);
}

void worker_main() {
  g_worker_alive.store(true);
  while (g_run.load()) {
    if (!step()) {
#ifdef ESP_PLATFORM
      vTaskDelay(pdMS_TO_TICKS(50));
#else
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
#endif
    }
  }
  g_worker_alive.store(false);
}
#ifdef ESP_PLATFORM
void worker_task(void*) {
  worker_main();
  vTaskDelete(nullptr);
}
#endif

}  // namespace

const char* error_name(Error error) {
  switch (error) {
    case Error::no_card: return "no card";
    case Error::no_space: return "card full";
    case Error::write_error: return "write error";
    case Error::rename_error: return "rename error";
    case Error::index_error: return "index error";
    case Error::too_few_rows: return "too few rows";
    case Error::queue_full: return "queue full";
    case Error::shutdown: return "shutdown";
    default: return "ok";
  }
}
const char* state_name(JobState state) {
  switch (state) {
    case JobState::pending: return "pending";
    case JobState::saving: return "saving";
    case JobState::saved: return "saved";
    case JobState::failed: return "failed";
    case JobState::skipped: return "skipped";
    default: return "none";
  }
}

const Io& default_io() {
  static const Io io = {d_mounted, d_make_dir, d_open, d_write, d_read, d_close,
                        d_rename,  d_remove,   d_exists, d_free_bytes, d_list_dir,
                        d_now_ms,  d_yield};
  return io;
}

void init(const Io* io_ops, const Config& config) {
  stop_worker();
  abandon_all(Error::shutdown);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_io = io_ops ? io_ops : &default_io();
  g_config = config;
  g_stats = Stats{};
  g_result_count = 0;
  g_index_ready = false;
  // Only receptions archived from now on are candidates; older gallery entries are not re-saved.
  g_last_serial = 0;
  for (size_t i = 0; i < orcsdr::sstv::archive_count(); ++i)
    g_last_serial = std::max(g_last_serial, orcsdr::sstv::archived_image(i).serial);
}

const Config& config() { return g_config; }
void set_enabled(bool enabled) { g_config.enabled = enabled; }
bool enabled() { return g_config.enabled; }

bool enqueue(const Image& pinned, uint32_t utc_epoch) {
  if (!g_io) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& job : g_jobs) {
    if (job.used) continue;
    job = Job{};
    job.used = true;
    job.image = pinned;
    job.utc = pinned.utc_epoch ? pinned.utc_epoch : utc_epoch;  // the reception's own stamp wins
    job.not_before = io().now_ms();
    ++g_stats.queued;
    ++g_stats.pending;
    note_result_locked(pinned.session_id, JobState::pending, Error::none, 0, 0, nullptr);
    return true;
  }
  ++g_stats.dropped_queue_full;
  g_stats.last_error = Error::queue_full;
  return false;
}

size_t poll(uint32_t utc_epoch) {
  if (!g_io) return 0;
  size_t newest_serial = 0;
  const size_t count = orcsdr::sstv::archive_count();
  for (size_t i = 0; i < count; ++i)
    newest_serial = std::max<size_t>(newest_serial, orcsdr::sstv::archived_image(i).serial);
  if (!g_config.enabled) {                 // auto-save off: remember, do not back-fill later
    g_last_serial = static_cast<uint32_t>(std::max<size_t>(g_last_serial, newest_serial));
    return 0;
  }
  size_t queued = 0;
  for (size_t n = count; n > 0; --n) {     // oldest first, so session order is preserved
    const size_t index = n - 1;
    const Image peek = orcsdr::sstv::archived_image(index);
    if (peek.serial <= g_last_serial) continue;
    if (peek.decoded_rows < g_config.min_rows) {
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_stats.skipped;
        note_result_locked(peek.session_id, JobState::skipped, Error::too_few_rows, 0, 0, nullptr);
      }
      g_last_serial = peek.serial;
      continue;
    }
    const Image pinned = orcsdr::sstv::pin_archived(index);
    if (!enqueue(pinned, utc_epoch)) {
      orcsdr::sstv::release_image(pinned); // still in the gallery: try again on the next poll
      break;
    }
    g_last_serial = peek.serial;
    ++queued;
  }
  return queued;
}

bool step() {
  if (!g_io) return false;
  Job job;
  size_t slot = kQueue;
  const uint32_t now = io().now_ms();
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < kQueue; ++i) {
      if (!g_jobs[i].used || g_jobs[i].busy) continue;
      if (static_cast<int32_t>(now - g_jobs[i].not_before) < 0) continue;
      slot = i;
      g_jobs[i].busy = true;
      job = g_jobs[i];
      note_result_locked(job.image.session_id, JobState::saving, Error::none, 0, 0, nullptr);
      break;
    }
    if (slot == kQueue) {                  // idle, or only waiting out a retry delay
      if (!g_recover_requested.exchange(false)) return false;
    }
  }
  if (slot == kQueue) {                    // idle and a recovery was asked for: do it now, off the UI task
    (void)recover();
    return false;
  }
  Result result;
  const uint32_t started = io().now_ms();
  const Outcome outcome = process(job, &result);
  const uint32_t elapsed = io().now_ms() - started;
  bool release = true;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (outcome == Outcome::retry && g_jobs[slot].attempts < g_config.retries) {
      ++g_jobs[slot].attempts;
      g_jobs[slot].busy = false;
      g_jobs[slot].not_before = io().now_ms() + g_config.retry_delay_ms;
      note_result_locked(job.image.session_id, JobState::pending, result.error, 0, 0, nullptr);
      release = false;                     // keep the pin while waiting for the card
    } else {
      g_jobs[slot] = Job{};
      if (g_stats.pending) --g_stats.pending;
      g_stats.last_write_ms = elapsed;
      if (elapsed > g_stats.max_write_ms) g_stats.max_write_ms = elapsed;
      if (outcome == Outcome::saved) {
        ++g_stats.saved;
        note_result_locked(job.image.session_id, JobState::saved, Error::none, elapsed, result.bytes,
                           result.filename);
      } else {
        ++g_stats.failed;
        g_stats.last_error = result.error;
        note_result_locked(job.image.session_id, JobState::failed, result.error, elapsed, 0,
                           result.filename);
      }
    }
  }
  if (release) orcsdr::sstv::release_image(job.image);
  std::lock_guard<std::mutex> lock(g_mutex);
  return ready_job_locked(io().now_ms());
}

bool start_worker() {
  if (!g_io) return false;
  if (g_run.load()) return true;
  g_run.store(true);
#ifdef ESP_PLATFORM
  if (xTaskCreate(worker_task, "sstv_arc", 6144, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    g_run.store(false);
    return false;
  }
#else
  g_thread = std::thread(worker_main);
#endif
  return true;
}

void stop_worker() {
  if (!g_run.exchange(false)) return;
#ifdef ESP_PLATFORM
  for (int i = 0; i < 200 && g_worker_alive.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
#else
  if (g_thread.joinable()) g_thread.join();
#endif
}

bool worker_running() { return g_run.load(); }
void request_recover() { g_recover_requested.store(true); }
bool initialised() { return g_io != nullptr; }

void drain(uint32_t max_ms) {
  if (!g_io) return;
  const uint32_t start = io().now_ms();
  while (io().now_ms() - start < max_ms) {
    if (!step()) {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!any_job_locked()) return;
    }
#ifndef ESP_PLATFORM
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  }
}

Stats stats() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_stats;
}

size_t recent_results(Result* out, size_t capacity) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const size_t n = std::min(capacity, g_result_count);
  for (size_t i = 0; i < n; ++i) out[i] = g_results[i];
  return n;
}

bool result_for(uint32_t session_id, Result* out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (size_t i = 0; i < g_result_count; ++i)
    if (g_results[i].session_id == session_id) {
      if (out) *out = g_results[i];
      return true;
    }
  return false;
}

// ---------------------------------------------------------------- recovery
namespace {
struct Names {
  char item[128][64];
  size_t count = 0;
};
void collect(const char* name, void* ctx) {
  auto* names = static_cast<Names*>(ctx);
  const size_t len = std::strlen(name);
  if (len < 5 || len >= 64 || names->count >= 128) return;
  if (std::strcmp(name + len - 5, ".part") == 0 ||
      (len > 4 && std::strcmp(name + len - 4, ".bmp") == 0))
    std::snprintf(names->item[names->count++], 64, "%s", name);
}
bool index_mentions(const char* name) {
  char path[96];
  index_path(path, sizeof(path));
  std::FILE* f = io().open(path, "rb");
  if (!f) return false;
  const size_t len = std::strlen(name);
  char window[1100];
  size_t carry = 0;
  bool found = false;
  for (;;) {
    const size_t got = io().read(window + carry, 1024, f);
    if (!got) break;
    const size_t total = carry + got;
    window[total] = '\0';
    if (std::strstr(window, name)) { found = true; break; }
    carry = std::min(total, len > 1 ? len - 1 : size_t{0});
    std::memmove(window, window + total - carry, carry);
  }
  io().close(f);
  return found;
}
}  // namespace

size_t recover() {
  if (!g_io || !io().mounted()) return 0;
  if (!make_dirs(g_config.root) || !ensure_index()) return 0;
  static Names names;                      // static: keeps 8 KiB off the worker's small stack
  names.count = 0;
  if (!io().list_dir(g_config.root, collect, &names)) return 0;
  size_t recovered = 0;
  for (size_t i = 0; i < names.count; ++i) {
    char path[160];
    std::snprintf(path, sizeof(path), "%s/%s", g_config.root, names.item[i]);
    const size_t len = std::strlen(names.item[i]);
    if (std::strcmp(names.item[i] + len - 5, ".part") == 0) { (void)io().remove(path); continue; }
    if (index_mentions(names.item[i])) continue;
    Row row;
    unsigned long utc = 0, freq = 0, session = 0;
    if (std::sscanf(names.item[i], "rx_%lu_%lu_%lu", &utc, &freq, &session) != 3) continue;
    row.session = static_cast<uint32_t>(session);
    row.utc = static_cast<uint32_t>(utc);
    row.utc_verified = utc != 0;
    row.frequency = static_cast<uint32_t>(freq);
    row.end = "recovered";
    row.file = names.item[i];
    if (std::FILE* f = io().open(path, "rb")) {
      uint8_t header[54] = {};
      if (io().read(header, sizeof(header), f) == sizeof(header) && header[0] == 'B' && header[1] == 'M') {
        row.width = static_cast<uint16_t>(header[18] | (header[19] << 8));
        row.height = static_cast<uint16_t>(header[22] | (header[23] << 8));
      }
      io().close(f);
    }
    if (append_index(row)) ++recovered;
  }
  return recovered;
}

}  // namespace orcsdr::sstv_archive
