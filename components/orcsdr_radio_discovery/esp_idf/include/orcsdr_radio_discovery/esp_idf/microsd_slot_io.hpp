#pragma once
// F0c: filesystem boundary for the F0b two-slot discovery journal.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_radio_discovery/library.hpp"
#include <cstddef>
#include <cstdint>
#include <sys/stat.h>
#include <sys/types.h>

namespace orcsdr::radio_discovery::esp_idf {

// Kept injectable so the exact VFS read/write/fsync/close failure paths are
// reproducible on a host. On ESP-IDF, NativeFilePort uses mounted FatFs VFS.
class FilePort {
public:
    virtual ~FilePort() = default;
    virtual int open_file(const char* path, int flags, mode_t mode) noexcept = 0;
    virtual ssize_t read_file(int fd, void* data, size_t n) noexcept = 0;
    virtual ssize_t write_file(int fd, const void* data, size_t n) noexcept = 0;
    virtual int sync_file(int fd) noexcept = 0;
    virtual int close_file(int fd) noexcept = 0;
    virtual int fd_stat(int fd, struct stat* out) noexcept = 0;
    // Non-ESP hosts use lstat to reject symlinks; FatFs has no symlinks.
    virtual int path_stat(const char* path, struct stat* out) noexcept = 0;
    virtual int make_dir(const char* path, mode_t mode) noexcept = 0;
};

class NativeFilePort final : public FilePort {
public:
    int open_file(const char*, int, mode_t) noexcept override;
    ssize_t read_file(int, void*, size_t) noexcept override;
    ssize_t write_file(int, const void*, size_t) noexcept override;
    int sync_file(int) noexcept override;
    int close_file(int) noexcept override;
    int fd_stat(int, struct stat*) noexcept override;
    int path_stat(const char*, struct stat*) noexcept override;
    int make_dir(const char*, mode_t) noexcept override;
};

enum class StorageError : uint8_t {
    none, invalid_mount_path, mount_unavailable, not_ready, invalid_slot,
    invalid_argument, bad_directory, mkdir_failed, open_failed, missing,
    stat_failed, not_regular, oversize, short_read, read_failed,
    short_write, write_failed, sync_failed, close_failed
};

// The only two slot paths are <mounted_root>/orcsdr/disc1/obs1{a,b}.bin.
// FAT 8.3 names deliberately work without long-filename support.
// This class never formats, mounts, removes, renames, or touches P25 files.
// Call begin() ONLY after the caller has successfully mounted the microSD;
// call end() before unmounting/removing it. Not thread-safe: serialize access.
class MicroSdSlotIO final : public SlotIO {
public:
    static constexpr size_t kMaxRootPath = 96;
    static constexpr size_t kMaxPath = 160;
    explicit MicroSdSlotIO(FilePort& files, size_t max_snapshot_bytes) noexcept;
    bool begin(const char* mounted_root) noexcept;
    void end() noexcept;
    IoState read(uint8_t slot, uint8_t* dst, size_t capacity, size_t& actual) override;
    bool write_durable(uint8_t slot, const uint8_t* src, size_t length) override;
    StorageError last_error() const noexcept { return error_; }
    int last_errno() const noexcept { return os_errno_; }
    bool ready() const noexcept { return ready_; }
    size_t max_snapshot_bytes() const noexcept { return max_bytes_; }
    // Introspection only. Never permit externally supplied filenames/slots.
    const char* slot_path(uint8_t slot) const noexcept;
private:
    void fail(StorageError error, int os_error = 0) noexcept;
    bool check_directory(const char* path) noexcept;
    bool ensure_directory(const char* path) noexcept;
    bool medium_ready() noexcept;
    bool validate_root(const char* path) noexcept;
    bool write_paths(const char* root) noexcept;
    FilePort& files_;
    size_t max_bytes_;
    char mount_[kMaxRootPath]{};
    char parent_[kMaxPath]{};
    char folder_[kMaxPath]{};
    char paths_[2][kMaxPath]{};
    StorageError error_ = StorageError::not_ready;
    int os_errno_ = 0;
    bool ready_ = false;
};

} // namespace orcsdr::radio_discovery::esp_idf
