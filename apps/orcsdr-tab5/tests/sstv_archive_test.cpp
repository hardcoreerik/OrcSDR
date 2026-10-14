// Host regression for the background SSTV SD archive: atomic files, v2 index, pinned
// image ownership, bounded queue, missing/full/removed/slow cards, recovery, and proof
// that a slow card cannot stall the decoder. Real files in a temp directory; faults
// are injected through the archive's Io interface.
#include "sstv_archive.hpp"
#include "sstv_receiver.hpp"
#include "sstv_test_signal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace arc = orcsdr::sstv_archive;
namespace rx = orcsdr::sstv;
using sstv_test::require;
using sstv_test::Signal;

namespace {

// ---------------------------------------------------------------- fault-injecting I/O
struct Fault {
  std::atomic<bool> mounted{true};
  std::atomic<uint64_t> free_bytes{~0ull};
  std::atomic<int> fail_after_writes{-1};  // fail the BMP after this many row writes, then "remove" the card
  std::atomic<int> write_delay_us{0};      // per BMP row write (a slow card)
  std::atomic<int> part_writes{0};
  std::atomic<int> eio_files{0};           // the next N temp files each fail with EIO after 3 block writes
  std::atomic<int> eio_active{0};          // this temp file is one of them
  std::atomic<int> read_failures{0};       // the next N reads fail with EIO (a flaky card)
  std::atomic<int> read_skip{0};           // let the next N reads succeed before read_failures starts
  std::atomic<int> short_reads{0};         // the next reads return only N, N-1, ... bytes (partial reads)
} g;
std::FILE* g_part_file = nullptr;

bool f_mounted() { return g.mounted.load(); }
bool f_make_dir(const char* path) {
  if (::mkdir(path, 0775) == 0) return true;
  struct stat st{};
  return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
std::FILE* f_open(const char* path, const char* mode) {
  if (!g.mounted.load()) return nullptr;
  std::FILE* f = std::fopen(path, mode);
  const size_t n = std::strlen(path);
  if (f && n > 5 && std::strcmp(path + n - 5, ".part") == 0) {
    g_part_file = f;
    g.part_writes = 0;
    g.eio_active = g.eio_files.load() > 0 ? 1 : 0;
    if (g.eio_active) g.eio_files--;
  }
  return f;
}
size_t f_write(const void* data, size_t size, std::FILE* file) {
  if (!g.mounted.load()) return 0;
  if (file == g_part_file) {
    const int writes = ++g.part_writes;
    if (g.eio_active.load() && writes > 3) { errno = EIO; return 0; }   // a transient card error
    const int limit = g.fail_after_writes.load();
    if (limit >= 0 && writes > limit) {
      g.mounted = false;  // card pulled mid-write
      return 0;
    }
    const int delay = g.write_delay_us.load();
    if (delay) std::this_thread::sleep_for(std::chrono::microseconds(delay));
  }
  return std::fwrite(data, 1, size, file);
}
size_t f_read(void* data, size_t size, std::FILE* file) {
  if (g.read_skip.load() > 0) { g.read_skip--; return std::fread(data, 1, size, file); }
  if (g.read_failures.load() > 0) { g.read_failures--; errno = EIO; return 0; }
  if (g.short_reads.load() > 0) {
    const size_t part = std::min<size_t>(size, static_cast<size_t>(g.short_reads.load()));
    g.short_reads--;
    return std::fread(data, 1, part, file);
  }
  return std::fread(data, 1, size, file);
}
int f_close(std::FILE* file) {
  if (file == g_part_file) g_part_file = nullptr;
  return std::fclose(file);
}
int f_rename(const char* a, const char* b) { return g.mounted.load() ? std::rename(a, b) : -1; }
int f_remove(const char* path) { return g.mounted.load() ? std::remove(path) : -1; }
bool f_exists(const char* path) {
  struct stat st{};
  return ::stat(path, &st) == 0;
}
uint64_t f_free() { return g.free_bytes.load(); }
bool f_list(const char* dir, void (*visit)(const char*, void*), void* ctx) {
  DIR* d = ::opendir(dir);
  if (!d) return false;
  while (const dirent* e = ::readdir(d)) visit(e->d_name, ctx);
  ::closedir(d);
  return true;
}
uint32_t f_now() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
void f_yield() { std::this_thread::yield(); }

const arc::Io kIo = {f_mounted, f_make_dir, f_open, f_write, f_read, f_close,
                     f_rename,  f_remove,   f_exists, f_free, f_list,  f_now,  f_yield};

// ---------------------------------------------------------------- helpers
std::string g_root;

void reset_world(unsigned retries = 0, uint32_t retry_delay_ms = 20) {
  rx::stop();
  rx::clear_gallery();
  rx::set_mode(rx::Mode::auto_vis);
  rx::set_auto_strategy(rx::AutoStrategy::vis_sync);
  rx::start(145800000);
  g.mounted = true;
  g.free_bytes = ~0ull;
  g.fail_after_writes = -1;
  g.write_delay_us = 0;
  g.eio_files = 0;
  g.eio_active = 0;
  g.read_failures = 0;
  g.short_reads = 0;
  g.read_skip = 0;
  std::string cmd = "rm -rf '" + g_root + "' && mkdir -p '" + g_root + "'";
  require(std::system(cmd.c_str()) == 0, "temp dir");
  arc::Config cfg;
  cfg.retries = static_cast<uint8_t>(retries);
  cfg.retry_delay_ms = retry_delay_ms;
  std::snprintf(cfg.root, sizeof(cfg.root), "%s/sstv", g_root.c_str());
  arc::init(&kIo, cfg);
}

// One complete, VIS-identified Robot 36 reception with a distinct neutral tone.
void produce(double luma_hz, Signal* sig = nullptr) {
  Signal local;
  Signal& s = sig ? *sig : local;
  s.luma_hz = luma_hz;
  s.header(8);
  s.robot36_rows();
  s.tone(1900, 50);
  s.silence(300);
  s.flush();
}

std::string slurp(const std::string& path) {
  std::string out;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
  }
  return out;
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    const size_t at = s.find(sep, start);
    if (at == std::string::npos) { if (start < s.size()) out.push_back(s.substr(start)); break; }
    out.push_back(s.substr(start, at - start));
    start = at + 1;
  }
  return out;
}
std::vector<std::string> list_names() {
  std::vector<std::string> out;
  const std::string dir = g_root + "/sstv";
  if (DIR* d = ::opendir(dir.c_str())) {
    while (const dirent* e = ::readdir(d)) out.push_back(e->d_name);
    ::closedir(d);
  }
  return out;
}
bool has_suffix(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
size_t count_suffix(const char* suffix) {
  size_t n = 0;
  for (const auto& name : list_names()) n += has_suffix(name, suffix);
  return n;
}
std::vector<std::vector<std::string>> index_rows() {
  std::vector<std::vector<std::string>> rows;
  const auto lines = split(slurp(g_root + "/sstv/index.csv"), '\n');
  for (size_t i = 1; i < lines.size(); ++i) rows.push_back(split(lines[i], ','));
  return rows;
}
uint16_t gray565(uint8_t v) {
  return static_cast<uint16_t>(((v & 248) << 8) | ((v & 252) << 3) | (v >> 3));
}
uint8_t luma_of(double hz) {
  int v = static_cast<int>((hz - 1500.) * 255. / 800. + .5);
  return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}
// Green channel of BMP pixel (x, y) from the top, 24-bit bottom-up.
int bmp_green(const std::string& file, unsigned width, unsigned height, unsigned x, unsigned y) {
  const unsigned stride = (width * 3u + 3u) & ~3u;
  const size_t off = 54 + static_cast<size_t>(height - 1 - y) * stride + x * 3u + 1;
  return off < file.size() ? static_cast<unsigned char>(file[off]) : -1;
}
void expect_content(const std::string& path, double luma_hz, const char* why) {
  const std::string file = slurp(path);
  require(file.size() == 54u + 960u * 240u, why);
  require(file[0] == 'B' && file[1] == 'M', why);
  const int want = ((gray565(luma_of(luma_hz)) >> 5) & 63) << 2;
  require(std::abs(bmp_green(file, 320, 240, 160, 50) - want) <= 12, why);
}

// ---------------------------------------------------------------- tests
void test_saves_complete_image_atomically() {
  reset_world();
  produce(2000);
  require(arc::poll(1700000000u) == 1, "save: finished image queued");
  arc::drain(5000);
  const auto stats = arc::stats();
  require(stats.saved == 1 && stats.failed == 0 && stats.pending == 0, "save: written");
  require(count_suffix(".part") == 0 && count_suffix(".bmp") == 1, "save: no .part left, one BMP");
  arc::Result res;
  require(arc::recent_results(&res, 1) == 1 && res.state == arc::JobState::saved, "save: result visible");
  expect_content(g_root + "/sstv/" + res.filename, 2000, "save: BMP header, size and pixels");
  const auto rows = index_rows();
  require(rows.size() == 1 && rows[0].size() == 18, "save: one v2 index row with 18 columns");
  const auto header = split(slurp(g_root + "/sstv/index.csv"), '\n')[0];
  require(header == arc::kIndexHeaderV2, "save: index starts with the v2 header");
  const auto& r = rows[0];
  require(r[1] == "1700000000" && r[2] == "1" && r[3] == "145800000", "save: clock verified, frequency");
  require(r[4] == "ROBOT 36" && r[5] == "8" && r[6] == "vis", "save: mode, VIS code, identification");
  require(r[8] == "320" && r[9] == "240" && r[10] == "240" && r[11] == "1" && r[12] == "0",
          "save: dimensions, decoded rows, complete, known start");
  require(r[15] == "complete" && r[17] == res.filename, "save: end reason and filename");
  require(rx::metrics().frames_alive == rx::archive_count(), "save: pin released after writing");
  rx::stop();
  rx::clear_gallery();
}

void test_partial_sync_image_is_labelled() {
  reset_world();
  Signal sig;
  sig.robot36_rows();           // no header: sync-identified, origin unknown
  sig.tone(1900, 1700);   // longer than the 8 missed lines (1.2 s of Robot 36) that end a picture
  sig.silence(300);
  sig.flush();
  require(rx::archive_count() == 1, "partial: image archived by the receiver");
  require(arc::poll(0) == 1, "partial: queued");
  arc::drain(5000);
  const auto rows = index_rows();
  require(rows.size() == 1, "partial: one index row");
  const auto& r = rows[0];
  require(r[1] == "0" && r[2] == "0", "partial: unverified clock is explicit");
  require(r[5] == "0" && r[6] == "sync", "partial: no VIS code, sync identification");
  require(r[11] == "0" && r[12] == "1" && r[15] == "sync_lost",
          "partial: not complete, unknown start, end reason recorded");
  require(std::atoi(r[10].c_str()) < 240, "partial: decoded rows below full height");
  rx::stop();
  rx::clear_gallery();
}

void test_identical_images_keep_every_event() {
  reset_world();
  produce(1900);
  produce(1900);
  require(arc::poll(1700000000u) == 2, "dup: both receptions queued");
  arc::drain(5000);
  const auto rows = index_rows();
  require(rows.size() == 2 && count_suffix(".bmp") == 2, "dup: two files, two rows");
  require(rows[0][0] != rows[1][0] && rows[0][17] != rows[1][17], "dup: distinct sessions and names");
  require(rows[0][16] == rows[1][16] && rows[0][16] != "0000000000000000",
          "dup: identical pixels share a content hash");
  rx::stop();
  rx::clear_gallery();
}

void test_missing_card_retries_then_recovers() {
  reset_world(/*retries=*/2, /*retry_delay_ms=*/20);
  g.mounted = false;
  produce(1800);
  require(arc::poll(1) == 1, "no card: queued");
  arc::drain(2000);
  auto stats = arc::stats();
  require(stats.failed == 1 && stats.saved == 0 && stats.last_error == arc::Error::no_card,
          "no card: fails with no_card after bounded retries");
  require(rx::metrics().frames_alive == rx::archive_count(), "no card: pin released on failure");
  g.mounted = true;
  produce(1850);
  require(arc::poll(1) == 1, "card back: next image queued");
  arc::drain(2000);
  stats = arc::stats();
  require(stats.saved == 1 && count_suffix(".bmp") == 1, "card back: saves normally");
  rx::stop();
  rx::clear_gallery();
}

void test_full_card_fails_cleanly() {
  reset_world();
  g.free_bytes = 100 * 1024;
  produce(1800);
  arc::poll(1);
  arc::drain(2000);
  const auto stats = arc::stats();
  require(stats.failed == 1 && stats.last_error == arc::Error::no_space, "full: failed with no_space");
  require(count_suffix(".part") == 0 && count_suffix(".bmp") == 0, "full: nothing partial left behind");
  require(rx::metrics().frames_alive == rx::archive_count(), "full: pin released");
  rx::stop();
  rx::clear_gallery();
}

void test_card_removed_mid_write() {
  reset_world(/*retries=*/3, /*retry_delay_ms=*/20);
  produce(1950);
  g.fail_after_writes = 5;     // pulled after five 16 KiB blocks
  arc::poll(1700000000u);
  arc::step();                 // first attempt dies mid-write
  require(g.mounted.load() == false, "removed: card was pulled mid-write");
  // A pulled card cannot delete its torn temp file either; it must never be exposed as a BMP.
  require(count_suffix(".bmp") == 0, "removed: no half-written BMP exposed");
  g.mounted = true;            // card reseated
  g.fail_after_writes = -1;
  arc::drain(2000);
  require(count_suffix(".part") == 0, "removed: retry overwrote and renamed the torn temp file");
  const auto stats = arc::stats();
  require(stats.saved == 1 && count_suffix(".bmp") == 1, "removed: retried and saved after reseat");
  arc::Result res;
  arc::recent_results(&res, 1);
  expect_content(g_root + "/sstv/" + res.filename, 1950, "removed: final BMP is complete");
  rx::stop();
  rx::clear_gallery();
}

// A transient card error (EIO) is absorbed by rewriting the temporary file; a persistent one is
// reported with its error code and leaves nothing behind.
void test_transient_card_errors() {
  reset_world();
  produce(1900);
  g.eio_files = 2;                   // the first two attempts fail, the third succeeds
  arc::poll(1700000000u);
  arc::drain(8000);
  auto stats = arc::stats();
  require(stats.saved == 1 && stats.failed == 0, "eio: saved after two transient failures");
  require(count_suffix(".part") == 0 && count_suffix(".bmp") == 1, "eio: one clean file, no temp leftovers");
  arc::Result res;
  arc::recent_results(&res, 1);
  expect_content(g_root + "/sstv/" + res.filename, 1900, "eio: the file written by the successful attempt is complete");
  require(res.os_error == 0, "eio: a recovered write reports no error");
  produce(1950);
  g.eio_files = 3;                   // every attempt fails
  arc::poll(1700000000u);
  arc::drain(8000);
  stats = arc::stats();
  require(stats.failed == 1 && stats.last_error == arc::Error::write_error, "eio: persistent errors fail the job");
  arc::recent_results(&res, 1);
  require(res.state == arc::JobState::failed && res.os_error == EIO, "eio: the OS error code is reported");
  require(count_suffix(".part") == 0 && count_suffix(".bmp") == 1, "eio: no half-written file left behind");
  require(rx::metrics().frames_alive == rx::archive_count(), "eio: pin released after the failure");
  rx::stop();
  rx::clear_gallery();
}

// A read error on the index must not be mistaken for an old-format index.
void test_failed_index_read_does_not_rotate_the_index() {
  reset_world();
  produce(1900);
  arc::poll(1700000000u);
  arc::drain(5000);
  require(arc::stats().saved == 1 && index_rows().size() == 1, "index read: first image indexed");
  arc::init(&kIo, arc::config());    // forget that the index was verified, so the next job reads the header again
  produce(1950);
  g.read_failures = 2;               // the first two reads of the next job fail, the third works
  arc::poll(1700000000u);
  arc::drain(5000);
  size_t rotated = 0;
  for (const auto& name : list_names()) rotated += name.rfind("index-v1-", 0) == 0;
  require(rotated == 0, "index read: a transient read error did not move the index aside");
  require(arc::stats().saved == 1 && index_rows().size() == 2, "index read: both images indexed in the original file");   // saved counts since the re-init
  // A card that stays unreadable fails the job but still leaves the index alone.
  arc::init(&kIo, arc::config());
  produce(2000);
  g.read_failures = 100;
  arc::poll(1700000000u);
  arc::drain(5000);
  g.read_failures = 0;
  rotated = 0;
  for (const auto& name : list_names()) rotated += name.rfind("index-v1-", 0) == 0;
  require(rotated == 0 && index_rows().size() == 2, "index read: an unreadable index is never replaced");
  require(arc::stats().failed == 1 && arc::stats().last_error == arc::Error::index_error,
          "index read: the job reports index_error");
  rx::stop();
  rx::clear_gallery();
}

// A partial read of the header is not an old-format index either (found in review).
void test_partial_index_read_does_not_rotate_the_index() {
  reset_world();
  produce(1900);
  arc::poll(1700000000u);
  arc::drain(5000);
  require(arc::stats().saved == 1 && index_rows().size() == 1, "partial read: first image indexed");
  arc::init(&kIo, arc::config());    // forget that the index was verified, so the next job reads the header again
  produce(1950);
  g.short_reads = 2;                 // header reads return 2 bytes, then 1 byte, then the whole header
  arc::poll(1700000000u);
  arc::drain(5000);
  size_t rotated = 0;
  for (const auto& name : list_names()) rotated += name.rfind("index-v1-", 0) == 0;
  require(rotated == 0, "partial read: a short header read did not move the index aside");
  require(arc::stats().saved == 1 && index_rows().size() == 2, "partial read: both images indexed in the original file");   // saved counts since the re-init
  rx::stop();
  rx::clear_gallery();
}

// A zero-byte index (power lost between creating it and writing the header) must not block saving for ever.
void test_empty_index_does_not_block_saving() {
  reset_world();
  const std::string dir = g_root + "/sstv";
  require(std::system(("mkdir -p '" + dir + "'").c_str()) == 0, "empty index: mkdir");
  std::FILE* f = std::fopen((dir + "/index.csv").c_str(), "wb");
  std::fclose(f);
  produce(1900);
  arc::poll(1700000000u);
  arc::drain(5000);
  require(arc::stats().saved == 1 && arc::stats().failed == 0, "empty index: the image was saved");
  require(index_rows().size() == 1, "empty index: a fresh index holds the row");
  rx::stop();
  rx::clear_gallery();
}

// Recovery must keep working when the card already holds more pictures than one pass can list.
void test_recovery_with_many_images_on_the_card() {
  reset_world();
  const std::string dir = g_root + "/sstv";
  require(std::system(("mkdir -p '" + dir + "'").c_str()) == 0, "many: mkdir");
  std::FILE* idx = std::fopen((dir + "/index.csv").c_str(), "wb");
  std::fprintf(idx, "%s\n", arc::kIndexHeaderV2);
  for (unsigned i = 1; i <= 170; ++i) {
    char name[96];
    std::snprintf(name, sizeof(name), "rx_1700000%03u_145800000_%u.bmp", i, i);
    if (i <= 150)   // 150 pictures are already indexed, the other 20 are orphans
      std::fprintf(idx, "%u,1700000%03u,1,145800000,ROBOT 36,8,vis,100,320,240,240,1,0,240,0,complete,0,%s\n", i, i, name);
    std::FILE* f = std::fopen((dir + "/" + name).c_str(), "wb");
    unsigned char bmp[54] = {'B', 'M'};
    std::fwrite(bmp, 1, sizeof(bmp), f);
    std::fclose(f);
  }
  std::fclose(idx);
  for (unsigned i = 0; i < 40; ++i) {     // more torn temp files than one pass removes
    char name[96];
    std::snprintf(name, sizeof(name), "rx_1700001%03u_145800000_9.bmp.part", i);
    std::FILE* f = std::fopen((dir + "/" + name).c_str(), "wb");
    std::fputs("torn", f);
    std::fclose(f);
  }
  g.read_skip = 1;                   // the header read succeeds, then the index read fails: not "nothing indexed"
  g.read_failures = 1;
  require(arc::recover() == 0 && index_rows().size() == 150, "many: a read error part-way through recovers nothing and duplicates nothing");
  g.read_failures = 0;
  require(arc::recover() == 20, "many: all 20 orphans were found among 170 pictures");
  size_t total = 20;
  for (int pass = 0; pass < 5 && count_suffix(".part") > 0; ++pass) total += arc::recover();
  require(count_suffix(".part") == 0, "many: every torn temp file was removed over a few passes");
  require(total == 20 && arc::recover() == 0, "many: nothing is indexed twice");
  rx::stop();
  rx::clear_gallery();
}

// Power lost in the middle of an index append: the next rows must not be glued onto the torn one.
void test_torn_index_tail_is_terminated_before_recovery() {
  reset_world();
  const std::string dir = g_root + "/sstv";
  require(std::system(("mkdir -p '" + dir + "'").c_str()) == 0, "torn: mkdir");
  std::FILE* idx = std::fopen((dir + "/index.csv").c_str(), "wb");
  std::fprintf(idx, "%s\n", arc::kIndexHeaderV2);
  std::fputs("1,1700000001,1,145800000,ROBOT 36,8,vis,100,320,240,240,1,0,240,0,complete,0,rx_1700000001_145800000_1.bmp\n", idx);
  std::fputs("2,1700000002,1,1458", idx);          // cut short, no newline
  std::fclose(idx);
  for (unsigned i = 1; i <= 2; ++i) {
    char name[96];
    std::snprintf(name, sizeof(name), "%s/rx_170000000%u_145800000_%u.bmp", dir.c_str(), i, i);
    std::FILE* f = std::fopen(name, "wb");
    unsigned char bmp[54] = {'B', 'M'};
    std::fwrite(bmp, 1, sizeof(bmp), f);
    std::fclose(f);
  }
  require(arc::recover() == 1, "torn: the orphan picture was recovered");
  const auto lines = split(slurp(dir + "/index.csv"), '\n');
  bool torn_alone = false, recovered_row_ok = false;
  for (const auto& line : lines) {
    if (line == "2,1700000002,1,1458") torn_alone = true;                 // the cut-short row stands on its own
    if (line.find("recovered") != std::string::npos)                       // and the new row is a whole row of its own
      recovered_row_ok = line.rfind("2,1700000002,", 0) == 0 && line.find("rx_1700000002_145800000_2.bmp") != std::string::npos &&
                         line.find("1458,") == std::string::npos && line.find("recovered") != std::string::npos &&
                         static_cast<size_t>(std::count(line.begin(), line.end(), ',')) == 17;
  }
  require(torn_alone && recovered_row_ok, "torn: the recovered row starts on its own line, not glued to the torn one");
  rx::stop();
  rx::clear_gallery();
}

void test_queue_is_bounded_and_never_blocks() {
  reset_world();
  for (int k = 0; k < 4; ++k) produce(1600 + 100. * k);
  require(rx::archive_count() == 4, "queue: four images in the gallery");
  const auto t0 = std::chrono::steady_clock::now();
  const size_t queued = arc::poll(1);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
  require(queued == 3 && arc::stats().dropped_queue_full == 1, "queue: holds three jobs, drops the fourth");
  require(ms < 200, "queue: poll never waits on the card");
  arc::drain(5000);
  require(arc::stats().saved == 3, "queue: the three queued images are saved");
  require(arc::poll(1) == 1, "queue: the dropped image is retried on the next poll");
  arc::drain(5000);
  require(arc::stats().saved == 4 && count_suffix(".bmp") == 4, "queue: all four saved, none lost");
  rx::stop();
  rx::clear_gallery();
}

void test_eviction_while_pending_keeps_pixels() {
  reset_world();
  produce(1700);
  require(arc::poll(1700000000u) == 1, "evict: first image queued");
  for (int k = 0; k < 5; ++k) produce(1800 + 60. * k);  // evicts the first from the gallery
  require(rx::metrics().frames_alive == 5, "evict: four gallery frames plus the pinned one");
  arc::drain(5000);
  arc::Result res;
  require(arc::result_for(1, &res) || arc::recent_results(&res, 1) == 1, "evict: result recorded");
  require(arc::stats().saved == 1, "evict: pending image written");
  bool found = false;
  for (const auto& name : list_names())
    if (has_suffix(name, ".bmp")) {
      expect_content(g_root + "/sstv/" + name, 1700, "evict: file holds the evicted image's pixels");
      found = true;
    }
  require(found, "evict: BMP exists");
  require(rx::metrics().frames_alive == 4, "evict: pin released after the write");
  rx::stop();
  rx::clear_gallery();
}

// A slow card must not slow the decoder, drop audio or block the UI task.
void test_slow_card_does_not_disturb_decoding() {
  reset_world();
  g.write_delay_us = 100000;         // 100 ms per 16 KiB block: about 1.5 s per image
  produce(2000);
  arc::poll(1700000000u);
  require(arc::start_worker(), "slow: worker started");
  const auto overruns_before = rx::metrics().audio_overruns;
  Signal sig;
  sig.luma_hz = 1700;
  long worst_us = 0;
  sig.header(8);
  // Decode a full transmission while the worker is busy writing the first image.
  const auto start = std::chrono::steady_clock::now();
  for (unsigned row = 0; row < 240; row += 10) {
    const auto t0 = std::chrono::steady_clock::now();
    sig.robot36_rows(row, 10);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    if (us > worst_us) worst_us = static_cast<long>(us);
  }
  sig.tone(1900, 50);
  sig.silence(300);
  sig.flush();
  const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
  std::fprintf(stderr, "slow: decode of a full image took %ld ms (worst 10-row slice %ld us) "
                       "while the card wrote at 100 ms per block\n", static_cast<long>(total_ms), worst_us);
  require(arc::stats().saved == 0 || arc::stats().pending <= 1, "slow: decode overlapped the write");
  require(rx::archive_count() == 2 && rx::archived_image(0).decoded_rows == 240,
          "slow: second image decoded completely during the slow write");
  require(rx::metrics().audio_overruns == overruns_before, "slow: no audio overruns");
  require(arc::poll(1700000001u) == 1, "slow: second image queued");
  for (int i = 0; i < 600 && arc::stats().pending > 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  arc::stop_worker();
  require(arc::stats().saved == 2 && arc::stats().failed == 0, "slow: both images eventually saved");
  require(arc::stats().max_write_ms >= 1000, "slow: write latency is measured and exposed");
  rx::stop();
  rx::clear_gallery();
}

void test_recovery_after_power_loss() {
  reset_world();
  produce(1900);
  arc::poll(1700000000u);
  arc::drain(5000);
  require(count_suffix(".bmp") == 1, "recover: BMP written");
  // Simulate power loss between the rename and the index append, plus a torn temp file.
  const std::string index = g_root + "/sstv/index.csv";
  std::FILE* f = std::fopen(index.c_str(), "wb");
  std::fprintf(f, "%s\n", arc::kIndexHeaderV2);
  std::fclose(f);
  f = std::fopen((g_root + "/sstv/rx_1700000009_145800000_5.bmp.part").c_str(), "wb");
  std::fputs("torn", f);
  std::fclose(f);
  require(arc::recover() == 1, "recover: orphan BMP re-indexed");
  require(count_suffix(".part") == 0, "recover: torn .part removed");
  const auto rows = index_rows();
  require(rows.size() == 1 && rows[0][15] == "recovered" && rows[0][8] == "320" && rows[0][9] == "240",
          "recover: row marked recovered with dimensions read from the BMP");
  require(arc::recover() == 0, "recover: idempotent");
  rx::stop();
  rx::clear_gallery();
}

void test_v1_index_is_preserved() {
  reset_world();
  const std::string dir = g_root + "/sstv";
  require(std::system(("mkdir -p '" + dir + "'").c_str()) == 0, "v1: mkdir");
  std::FILE* f = std::fopen((dir + "/index.csv").c_str(), "wb");
  std::fputs("session_id,utc_epoch,frequency_hz,mode,width,height,decoded_rows,partial,sync_missed,filename\n"
             "1,0,145800000,ROBOT 36,320,240,240,0,0,old.bmp\n", f);
  std::fclose(f);
  produce(1900);
  arc::poll(1700000000u);
  arc::drain(5000);
  size_t old_files = 0;
  for (const auto& name : list_names())
    if (name.rfind("index-v1-", 0) == 0) {
      ++old_files;
      require(slurp(dir + "/" + name).find("old.bmp") != std::string::npos, "v1: old rows untouched");
    }
  require(old_files == 1, "v1: previous index moved aside, not rewritten");
  require(split(slurp(dir + "/index.csv"), '\n')[0] == arc::kIndexHeaderV2 && index_rows().size() == 1,
          "v1: fresh v2 index started");
  rx::stop();
  rx::clear_gallery();
}

void test_autosave_switch_and_min_rows() {
  reset_world();
  arc::set_enabled(false);
  produce(1900);
  require(arc::poll(1) == 0, "switch: auto-save off queues nothing");
  arc::set_enabled(true);
  produce(1950);
  require(arc::poll(1) == 1, "switch: only receptions after enabling are saved (no back-fill)");
  arc::drain(5000);
  // Too few rows is noise, not an image.
  Signal sig;
  sig.header(8);
  sig.robot36_rows(0, 2);
  sig.silence(3000);
  sig.flush();
  const size_t before = arc::stats().saved;
  require(arc::poll(1) == 0 && arc::stats().skipped == 1, "switch: a 3-row reception is skipped");
  arc::drain(1000);
  require(arc::stats().saved == before, "switch: skipped reception is not written");
  rx::stop();
  rx::clear_gallery();
}

}  // namespace

int main() {
  char tmpl[] = "/tmp/sstvarc-XXXXXX";
  require(::mkdtemp(tmpl) != nullptr, "mkdtemp");
  g_root = tmpl;
  test_saves_complete_image_atomically();
  test_partial_sync_image_is_labelled();
  test_identical_images_keep_every_event();
  test_missing_card_retries_then_recovers();
  test_full_card_fails_cleanly();
  test_card_removed_mid_write();
  test_transient_card_errors();
  test_failed_index_read_does_not_rotate_the_index();
  test_partial_index_read_does_not_rotate_the_index();
  test_empty_index_does_not_block_saving();
  test_recovery_with_many_images_on_the_card();
  test_torn_index_tail_is_terminated_before_recovery();
  test_queue_is_bounded_and_never_blocks();
  test_eviction_while_pending_keeps_pixels();
  test_slow_card_does_not_disturb_decoding();
  test_recovery_after_power_loss();
  test_v1_index_is_preserved();
  test_autosave_switch_and_min_rows();
  arc::stop_worker();
  const std::string cmd = "rm -rf '" + g_root + "'";
  if (std::system(cmd.c_str()) != 0) std::fputs("note: temp dir cleanup failed\n", stderr);
  std::puts("SSTV archive: PASS");
  return 0;
}
