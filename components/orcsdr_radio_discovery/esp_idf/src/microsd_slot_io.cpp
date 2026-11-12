// F0c ESP-IDF FatFs/VFS-backed microSD SlotIO. No firmware registration.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_radio_discovery/esp_idf/microsd_slot_io.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace orcsdr::radio_discovery::esp_idf {

int NativeFilePort::open_file(const char* p, int flags, mode_t mode) noexcept { return ::open(p, flags, mode); }
ssize_t NativeFilePort::read_file(int fd, void* p, size_t n) noexcept { return ::read(fd, p, n); }
ssize_t NativeFilePort::write_file(int fd, const void* p, size_t n) noexcept { return ::write(fd, p, n); }
int NativeFilePort::sync_file(int fd) noexcept { return ::fsync(fd); }
int NativeFilePort::close_file(int fd) noexcept { return ::close(fd); }
int NativeFilePort::fd_stat(int fd, struct stat* out) noexcept { return ::fstat(fd, out); }
int NativeFilePort::path_stat(const char* p, struct stat* out) noexcept {
#if defined(ESP_PLATFORM)
    return ::stat(p, out); // FatFs has no symbolic links.
#else
    return ::lstat(p, out); // Reject symbolic links in host test paths.
#endif
}
int NativeFilePort::make_dir(const char* p, mode_t mode) noexcept { return ::mkdir(p, mode); }

MicroSdSlotIO::MicroSdSlotIO(FilePort& files, size_t max_snapshot_bytes) noexcept
    : files_(files), max_bytes_(max_snapshot_bytes) {}

void MicroSdSlotIO::fail(StorageError error, int os_error) noexcept {
    error_ = error;
    os_errno_ = os_error;
}

void MicroSdSlotIO::end() noexcept {
    ready_ = false;
    mount_[0] = parent_[0] = folder_[0] = paths_[0][0] = paths_[1][0] = '\0';
    fail(StorageError::not_ready);
}

// Absolute canonical-looking mount root. In particular, do not accept a path
// inside /orcsdr, /p25, a path using '..', or an absolute root '/'. The two
// basename choices themselves are compile-time constants, not user input.
bool MicroSdSlotIO::validate_root(const char* path) noexcept {
    if (!path || path[0] != '/' || path[1] == '\0') return false;
    size_t n = 0;
    while (n < kMaxRootPath && path[n] != '\0') ++n;
    if (n < 2 || n >= kMaxRootPath || path[n - 1] == '/') return false;
    size_t segment = 1;
    for (size_t i = 1; i <= n; ++i) {
        const char c = path[i];
        if (c == '/' || c == '\0') {
            if (i == segment || (i - segment == 1 && path[segment] == '.') ||
                (i - segment == 2 && path[segment] == '.' && path[segment + 1] == '.')) return false;
            const size_t len = i - segment;
            // Prevent caller from treating a P25/profile/discovery subfolder as
            // the mount root. Case-insensitive for FAT volumes.
            if (len == 3 && (path[segment] == 'p' || path[segment] == 'P') &&
                path[segment + 1] == '2' && path[segment + 2] == '5') return false;
            if (len == 6) {
                static constexpr char reserved[] = "orcsdr";
                bool match = true;
                for (size_t k = 0; k < 6; ++k) {
                    const char v = path[segment + k];
                    if (v != reserved[k] && v != static_cast<char>(reserved[k] - 'a' + 'A')) match = false;
                }
                if (match) return false;
            }
            segment = i + 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool MicroSdSlotIO::write_paths(const char* root) noexcept {
    if (std::snprintf(mount_, sizeof(mount_), "%s", root) < 0 ||
        std::snprintf(parent_, sizeof(parent_), "%s/orcsdr", root) < 0 ||
        std::snprintf(folder_, sizeof(folder_), "%s/orcsdr/disc1", root) < 0 ||
        std::snprintf(paths_[0], sizeof(paths_[0]), "%s/orcsdr/disc1/obs1a.bin", root) < 0 ||
        std::snprintf(paths_[1], sizeof(paths_[1]), "%s/orcsdr/disc1/obs1b.bin", root) < 0) return false;
    return std::strlen(mount_) < sizeof(mount_) - 1 &&
           std::strlen(parent_) < sizeof(parent_) - 1 &&
           std::strlen(folder_) < sizeof(folder_) - 1 &&
           std::strlen(paths_[0]) < sizeof(paths_[0]) - 1 &&
           std::strlen(paths_[1]) < sizeof(paths_[1]) - 1;
}

bool MicroSdSlotIO::check_directory(const char* path) noexcept {
    struct stat st{};
    if (files_.path_stat(path, &st) != 0) {
        fail(StorageError::mount_unavailable, errno);
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        fail(StorageError::bad_directory);
        return false;
    }
    return true;
}

bool MicroSdSlotIO::ensure_directory(const char* path) noexcept {
    struct stat st{};
    if (files_.path_stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fail(StorageError::bad_directory);
            return false;
        }
        return true;
    }
    if (errno != ENOENT) {
        fail(StorageError::bad_directory, errno);
        return false;
    }
    if (files_.make_dir(path, 0700) != 0 && errno != EEXIST) {
        fail(StorageError::mkdir_failed, errno);
        return false;
    }
    // EEXIST after a race is not enough: ensure it is really a directory.
    if (files_.path_stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fail(StorageError::bad_directory, errno);
        return false;
    }
    return true;
}

bool MicroSdSlotIO::begin(const char* mounted_root) noexcept {
    end();
    if (!validate_root(mounted_root) || max_bytes_ == 0 || !write_paths(mounted_root)) {
        end();
        fail(StorageError::invalid_mount_path);
        return false;
    }
    // Check every mount component (including intermediate symlinks on host).
    char prefix[kMaxRootPath]{};
    const size_t n = std::strlen(mount_);
    std::memcpy(prefix, mount_, n + 1);
    for (size_t i = 1; i <= n; ++i) {
        if (i != n && prefix[i] != '/') continue;
        const char c = prefix[i];
        prefix[i] = '\0';
        if (!check_directory(prefix)) { end(); fail(StorageError::mount_unavailable); return false; }
        prefix[i] = c;
    }
    if (!ensure_directory(parent_) || !ensure_directory(folder_)) {
        const StorageError err = error_; const int os = os_errno_;
        end(); fail(err, os); return false;
    }
    ready_ = true;
    fail(StorageError::none);
    return true;
}

bool MicroSdSlotIO::medium_ready() noexcept {
    if (!ready_) { fail(StorageError::not_ready); return false; }
    // An unmounted/missing directory is a hard error, not an empty journal.
    if (!check_directory(mount_) || !check_directory(parent_) || !check_directory(folder_)) {
        fail(StorageError::mount_unavailable, os_errno_);
        return false;
    }
    return true;
}

const char* MicroSdSlotIO::slot_path(uint8_t slot) const noexcept {
    return ready_ && slot < 2 ? paths_[slot] : nullptr;
}

IoState MicroSdSlotIO::read(uint8_t slot, uint8_t* dst, size_t capacity, size_t& actual) {
    actual = 0;
    if (slot >= 2) { fail(StorageError::invalid_slot); return IoState::error; }
    if (!medium_ready()) return IoState::error;
    if (!dst || capacity == 0) { fail(StorageError::invalid_argument); return IoState::error; }
    int flags = O_RDONLY;
#if !defined(ESP_PLATFORM) && defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if !defined(ESP_PLATFORM) && defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const int fd = files_.open_file(paths_[slot], flags, 0);
    if (fd < 0) {
        const int saved = errno;
        if (saved == ENOENT) { fail(StorageError::missing, saved); return IoState::missing; }
        fail(StorageError::open_failed, saved);
        return IoState::error;
    }
    StorageError error = StorageError::none;
    int saved = 0;
    struct stat st{};
    if (files_.fd_stat(fd, &st) != 0) { error = StorageError::stat_failed; saved = errno; }
    else if (!S_ISREG(st.st_mode)) { error = StorageError::not_regular; }
    else if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) > max_bytes_ ||
             static_cast<uint64_t>(st.st_size) > capacity) { error = StorageError::oversize; }
    else {
        const size_t wanted = static_cast<size_t>(st.st_size);
        size_t got = 0;
        while (got < wanted) {
            const ssize_t count = files_.read_file(fd, dst + got, wanted - got);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) { error = StorageError::read_failed; saved = errno; break; }
            if (count == 0) { error = StorageError::short_read; break; }
            if (static_cast<size_t>(count) > wanted - got) { error = StorageError::read_failed; break; }
            got += static_cast<size_t>(count);
        }
        if (error == StorageError::none) {
            // Refuse a file which grew after fstat, without writing past cap.
            uint8_t probe = 0;
            ssize_t count;
            do { count = files_.read_file(fd, &probe, 1); } while (count < 0 && errno == EINTR);
            if (count < 0) { error = StorageError::read_failed; saved = errno; }
            else if (count != 0) { error = StorageError::oversize; }
            else actual = wanted;
        }
    }
    if (files_.close_file(fd) != 0 && error == StorageError::none) {
        error = StorageError::close_failed; saved = errno;
    }
    if (error != StorageError::none) { actual = 0; fail(error, saved); return IoState::error; }
    fail(StorageError::none);
    return IoState::ok;
}

bool MicroSdSlotIO::write_durable(uint8_t slot, const uint8_t* src, size_t length) {
    if (slot >= 2) { fail(StorageError::invalid_slot); return false; }
    if (!medium_ready()) return false;
    if (!src || length == 0 || length > max_bytes_) {
        fail(StorageError::invalid_argument); return false;
    }
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#if !defined(ESP_PLATFORM) && defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if !defined(ESP_PLATFORM) && defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const int fd = files_.open_file(paths_[slot], flags, 0600);
    if (fd < 0) { fail(StorageError::open_failed, errno); return false; }
    StorageError error = StorageError::none;
    int saved = 0;
    size_t written = 0;
    while (written < length) {
        const ssize_t count = files_.write_file(fd, src + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { error = StorageError::write_failed; saved = errno; break; }
        if (count == 0) { error = StorageError::short_write; break; }
        if (static_cast<size_t>(count) > length - written) { error = StorageError::write_failed; break; }
        written += static_cast<size_t>(count);
    }
    if (error == StorageError::none && files_.sync_file(fd) != 0) {
        error = StorageError::sync_failed; saved = errno;
    }
    if (files_.close_file(fd) != 0 && error == StorageError::none) {
        error = StorageError::close_failed; saved = errno;
    }
    if (error != StorageError::none) { fail(error, saved); return false; }
    fail(StorageError::none);
    return true;
}

} // namespace orcsdr::radio_discovery::esp_idf
