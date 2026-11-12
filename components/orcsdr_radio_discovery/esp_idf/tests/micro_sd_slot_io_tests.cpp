// Host POSIX filesystem and fault injection tests for the ESP-IDF VFS adapter.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_radio_discovery/esp_idf/microsd_slot_io.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace orcsdr::radio_discovery;
using namespace orcsdr::radio_discovery::esp_idf;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d FAIL %s\n", __FILE__, __LINE__, #cond); std::abort(); } } while (false)

namespace {
struct TestPort final : FilePort {
    NativeFilePort native{};
    enum class Failure { none, open, stat, mkdir, read, early_eof, write, zero_write, sync, close } failure = Failure::none;
    bool short_chunks = false;
    bool interrupt_once = false;
    bool interrupted = false;
    unsigned written_calls = 0;
    int open_file(const char* p, int flags, mode_t mode) noexcept override {
        if (failure == Failure::open) { errno = EACCES; return -1; }
        return native.open_file(p, flags, mode);
    }
    ssize_t read_file(int fd, void* p, size_t n) noexcept override {
        if (failure == Failure::early_eof) return 0;
        if (failure == Failure::read) { errno = EIO; return -1; }
        if (interrupt_once && !interrupted) { interrupted = true; errno = EINTR; return -1; }
        return native.read_file(fd, p, short_chunks && n > 3 ? 3 : n);
    }
    ssize_t write_file(int fd, const void* p, size_t n) noexcept override {
        ++written_calls;
        // Partial write then fail or return zero; journal must keep prior slot.
        if (failure == Failure::write && written_calls >= 2) { errno = EIO; return -1; }
        if (failure == Failure::zero_write && written_calls >= 2) return 0;
        if (interrupt_once && !interrupted) { interrupted = true; errno = EINTR; return -1; }
        return native.write_file(fd, p, (short_chunks || failure == Failure::write || failure == Failure::zero_write) && n > 3 ? 3 : n);
    }
    int sync_file(int fd) noexcept override {
        if (failure == Failure::sync) { errno = EIO; return -1; }
        return native.sync_file(fd);
    }
    int close_file(int fd) noexcept override {
        const int result = native.close_file(fd);
        if (failure == Failure::close) { errno = EIO; return -1; }
        return result;
    }
    int fd_stat(int fd, struct stat* out) noexcept override {
        if (failure == Failure::stat) { errno = EIO; return -1; }
        return native.fd_stat(fd, out);
    }
    int path_stat(const char* path, struct stat* out) noexcept override { return native.path_stat(path, out); }
    int make_dir(const char* path, mode_t mode) noexcept override {
        if (failure == Failure::mkdir) { errno = EACCES; return -1; }
        return native.make_dir(path, mode);
    }
    void clear() { failure = Failure::none; short_chunks = false; interrupt_once = false; interrupted = false; written_calls = 0; }
};

struct Fixture {
    char root[72] = "/tmp/orcf0c-XXXXXX";
    TestPort sys;
    MicroSdSlotIO io;
    explicit Fixture(size_t maximum) : io(sys, maximum) {
        CHECK(::mkdtemp(root) != nullptr);
        CHECK(io.begin(root));
        CHECK(io.ready());
    }
    ~Fixture() {
        for (int i = 0; i != 2; ++i) {
            // Expected in the test only. The production adapter never unlinks.
            char p[160]; std::snprintf(p, sizeof(p), "%s/orcsdr/disc1/obs1%c.bin", root, i ? 'b' : 'a');
            (void)::unlink(p);
        }
        char path[160];
        std::snprintf(path, sizeof(path), "%s/orcsdr/disc1", root); (void)::rmdir(path);
        std::snprintf(path, sizeof(path), "%s/orcsdr", root); (void)::rmdir(path);
        (void)::rmdir(root);
    }
};

void test_paths_and_files() {
    Fixture f(64);
    CHECK(std::strstr(f.io.slot_path(0), "/orcsdr/disc1/obs1a.bin") != nullptr);
    CHECK(std::strstr(f.io.slot_path(1), "/orcsdr/disc1/obs1b.bin") != nullptr);
    CHECK(!f.io.slot_path(2));
    std::array<uint8_t, 64> data{};
    size_t got = 9;
    CHECK(f.io.read(0, data.data(), data.size(), got) == IoState::missing);
    CHECK(got == 0 && f.io.last_error() == StorageError::missing);
    CHECK(!f.io.write_durable(2, data.data(), 8) && f.io.last_error() == StorageError::invalid_slot);
    CHECK(f.io.read(2, data.data(), data.size(), got) == IoState::error);
    CHECK(!f.io.write_durable(0, data.data(), 65));
    CHECK(!f.io.write_durable(0, nullptr, 8));
    CHECK(!f.io.write_durable(0, data.data(), 0));
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i + 1);
    CHECK(f.io.write_durable(0, data.data(), 64));
    std::array<uint8_t, 64> restored{};
    CHECK(f.io.read(0, restored.data(), restored.size(), got) == IoState::ok && got == 64);
    CHECK(restored == data);
    CHECK(f.io.read(0, restored.data(), 20, got) == IoState::error && got == 0);
    CHECK(f.io.last_error() == StorageError::oversize);
    CHECK(f.io.read(1, restored.data(), restored.size(), got) == IoState::missing);
    // Demonstrate bounded reads: oversize file rejected before buffer writes.
    restored.fill(0x7C);
    CHECK(f.io.read(0, restored.data(), 3, got) == IoState::error);
    for (auto b : restored) CHECK(b == 0x7C);
    f.io.end();
    CHECK(f.io.read(0, restored.data(), restored.size(), got) == IoState::error);
    CHECK(f.io.last_error() == StorageError::not_ready);
    CHECK(!f.io.write_durable(0, data.data(), 3));
    CHECK(f.io.begin(f.root));
    CHECK(f.io.read(0, restored.data(), restored.size(), got) == IoState::ok && got == 64);
}

void test_failures() {
    Fixture f(64);
    std::array<uint8_t, 64> src{};
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i);
    f.sys.short_chunks = true;
    f.sys.interrupt_once = true;
    CHECK(f.io.write_durable(0, src.data(), src.size()));
    f.sys.clear(); f.sys.short_chunks = true; f.sys.interrupt_once = true;
    std::array<uint8_t, 64> gotbytes{};
    size_t got = 0;
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::ok);
    CHECK(got == src.size() && gotbytes == src);
    f.sys.clear(); f.sys.failure = TestPort::Failure::open;
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::error);
    CHECK(f.io.last_error() == StorageError::open_failed && f.io.last_errno() == EACCES);
    f.sys.clear(); f.sys.failure = TestPort::Failure::stat;
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::error);
    CHECK(f.io.last_error() == StorageError::stat_failed);
    f.sys.clear(); f.sys.failure = TestPort::Failure::read;
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::error);
    CHECK(f.io.last_error() == StorageError::read_failed);
    f.sys.clear(); f.sys.failure = TestPort::Failure::early_eof;
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::error);
    CHECK(f.io.last_error() == StorageError::short_read);
    f.sys.clear(); f.sys.failure = TestPort::Failure::close;
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::error);
    CHECK(f.io.last_error() == StorageError::close_failed);
    f.sys.clear(); f.sys.failure = TestPort::Failure::write;
    CHECK(!f.io.write_durable(1, src.data(), src.size()));
    CHECK(f.io.last_error() == StorageError::write_failed);
    f.sys.clear(); f.sys.failure = TestPort::Failure::zero_write;
    CHECK(!f.io.write_durable(1, src.data(), src.size()));
    CHECK(f.io.last_error() == StorageError::short_write);
    f.sys.clear(); f.sys.failure = TestPort::Failure::sync;
    CHECK(!f.io.write_durable(1, src.data(), src.size()));
    CHECK(f.io.last_error() == StorageError::sync_failed);
    f.sys.clear(); f.sys.failure = TestPort::Failure::close;
    CHECK(!f.io.write_durable(1, src.data(), src.size()));
    CHECK(f.io.last_error() == StorageError::close_failed);
    f.sys.clear();
    CHECK(f.io.read(0, gotbytes.data(), gotbytes.size(), got) == IoState::ok);
    CHECK(gotbytes == src); // The other slot is never touched.
}

void test_reject_dangerous_paths() {
    Fixture f(64);
    CHECK(!f.io.begin("/"));
    CHECK(f.io.last_error() == StorageError::invalid_mount_path);
    CHECK(!f.io.begin("/sdcard/../p25"));
    CHECK(!f.io.begin("/sdcard/orcsdr"));
    CHECK(!f.io.begin("/sdcard/P25"));
    CHECK(!f.io.begin("relative"));
    CHECK(!f.io.begin("/tmp//bad"));
    CHECK(!f.io.begin("/tmp/./bad"));
    CHECK(!f.io.begin("/tmp/invalid space"));
    CHECK(!f.io.begin("/no_such_sdcard_f0c"));
    CHECK(f.io.last_error() == StorageError::mount_unavailable);
    // The mount directory itself must not be a symlink (host case).
    char link[80]; std::snprintf(link, sizeof(link), "%s-link", f.root);
    CHECK(::symlink(f.root, link) == 0);
    CHECK(!f.io.begin(link));
    CHECK(::unlink(link) == 0);
    CHECK(f.io.begin(f.root));
    // A missing backing directory after a mount/dropout must not masquerade as
    // an empty journal (which could trigger a destructive initial save).
    char original[160], moved[160];
    std::snprintf(original, sizeof(original), "%s/orcsdr/disc1", f.root);
    std::snprintf(moved, sizeof(moved), "%s/orcsdr/offline", f.root);
    CHECK(::rename(original, moved) == 0);
    std::array<uint8_t, 64> scratch{};
    size_t actual = 99;
    CHECK(f.io.read(0, scratch.data(), scratch.size(), actual) == IoState::error);
    CHECK(actual == 0 && f.io.last_error() == StorageError::mount_unavailable);
    CHECK(::rename(moved, original) == 0);
}

void test_provisioning_and_profile_isolation() {
    char mount[] = "/tmp/orcf0c-init-XXXXXX";
    CHECK(::mkdtemp(mount) != nullptr);
    TestPort port;
    MicroSdSlotIO io(port, 64);
    port.failure = TestPort::Failure::mkdir;
    CHECK(!io.begin(mount));
    CHECK(io.last_error() == StorageError::mkdir_failed);
    port.clear();
    // A pre-existing file at the expected directory path must block init.
    char conflict[160];
    std::snprintf(conflict, sizeof(conflict), "%s/orcsdr", mount);
    int fd = ::open(conflict, O_CREAT | O_WRONLY, 0600);
    CHECK(fd >= 0 && ::close(fd) == 0);
    CHECK(!io.begin(mount));
    CHECK(io.last_error() == StorageError::bad_directory);
    CHECK(::unlink(conflict) == 0);
    // Existing P25 profiles remain byte-for-byte untouched.
    char p25[160];
    std::snprintf(p25, sizeof(p25), "%s/P25.cfg", mount);
    fd = ::open(p25, O_CREAT | O_WRONLY, 0600);
    CHECK(fd >= 0);
    const char profile[] = "legacy P25 profile - leave unchanged";
    CHECK(::write(fd, profile, sizeof(profile)) == static_cast<ssize_t>(sizeof(profile)));
    CHECK(::close(fd) == 0);
    CHECK(io.begin(mount));
    const std::array<uint8_t, 4> payload{{1, 2, 3, 4}};
    CHECK(io.write_durable(0, payload.data(), payload.size()));
    fd = ::open(p25, O_RDONLY); CHECK(fd >= 0);
    char observed[sizeof(profile)]{};
    CHECK(::read(fd, observed, sizeof(observed)) == static_cast<ssize_t>(sizeof(observed)));
    CHECK(::close(fd) == 0);
    CHECK(std::memcmp(profile, observed, sizeof(profile)) == 0);
    CHECK(::unlink(io.slot_path(0)) == 0);
    io.end();
    char dir[160];
    std::snprintf(dir, sizeof(dir), "%s/orcsdr/disc1", mount); CHECK(::rmdir(dir) == 0);
    std::snprintf(dir, sizeof(dir), "%s/orcsdr", mount); CHECK(::rmdir(dir) == 0);
    CHECK(::unlink(p25) == 0);
    CHECK(::rmdir(mount) == 0);
}

using Db = Library<2,2,2>;
using J = Journal<2,2,2>;
Observation observation() {
    Observation o{};
    o.protocol = Protocol::dmr_tier2;
    o.origin = Origin::rf_observation;
    o.proof = Proof::crc_valid_decode;
    o.frequency_hz = 446500000;
    o.utc_known = true;
    o.utc_ms = 1000;
    o.identity.known_fields = access_code_known | slot_known;
    o.identity.access_code = 1;
    o.identity.slot = 1;
    return o;
}
void corrupt(const char* path) {
    const int fd = ::open(path, O_WRONLY);
    CHECK(fd >= 0);
    CHECK(::lseek(fd, 10, SEEK_SET) >= 0);
    const char c = 0x2F;
    CHECK(::write(fd, &c, 1) == 1);
    CHECK(::fsync(fd) == 0);
    CHECK(::close(fd) == 0);
}
void test_actual_journal_recovery() {
    Fixture f(Db::max_wire_bytes);
    Db db{}, loaded{};
    J writer{}, restart{};
    J::Workspace work{};
    CHECK(writer.load(f.io, loaded, work) == LoadState::empty);
    CHECK(db.observe(observation()) == Decision::added);
    CHECK(writer.save(f.io, db, work) == SaveState::ok);
    CHECK(writer.generation() == 1 && writer.active_slot() == 0);
    CHECK(restart.load(f.io, loaded, work) == LoadState::ok);
    CHECK(loaded.rf_count == 1 && loaded.rf[0].encounters == 1);
    loaded.rf[0].encounters = 2;
    CHECK(restart.save(f.io, loaded, work) == SaveState::ok);
    CHECK(restart.generation() == 2 && restart.active_slot() == 1);
    J reboot{}; Db again{};
    CHECK(reboot.load(f.io, again, work) == LoadState::ok);
    CHECK(again.rf[0].encounters == 2);
    corrupt(f.io.slot_path(1));
    CHECK(reboot.load(f.io, again, work) == LoadState::recovered);
    CHECK(reboot.generation() == 1 && reboot.active_slot() == 0);
    CHECK(again.rf[0].encounters == 1);
    f.sys.failure = TestPort::Failure::write;
    CHECK(reboot.save(f.io, again, work) == SaveState::io_error);
    f.sys.clear();
    J new_reboot{};
    CHECK(new_reboot.load(f.io, again, work) == LoadState::recovered);
    CHECK(new_reboot.generation() == 1);
    CHECK(new_reboot.save(f.io, again, work) == SaveState::ok);
    CHECK(new_reboot.generation() == 2);
    f.sys.failure = TestPort::Failure::sync;
    CHECK(new_reboot.save(f.io, again, work) == SaveState::io_error);
    f.sys.clear();
    CHECK(new_reboot.generation() == 2);
    // After an uncertain sync outcome, reload before deciding what committed.
    J final_reboot{};
    CHECK(final_reboot.load(f.io, again, work) == LoadState::ok);
    CHECK(final_reboot.generation() >= 2);
}
} // namespace

int main() {
    test_paths_and_files();
    test_failures();
    test_reject_dangerous_paths();
    test_provisioning_and_profile_isolation();
    test_actual_journal_recovery();
    std::puts("micro_sd_slot_io_tests PASS (POSIX host only)");
}
