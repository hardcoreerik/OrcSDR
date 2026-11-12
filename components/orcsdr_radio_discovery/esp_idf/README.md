# OrcSDR Radio Discovery — F0c microSD SlotIO (opt-in)

**Status:** independent, host-tested adapter and explicit integration recipe, **not wired to any firmware**. No ESP32-P4 compile, real SD card, card-removal, or power-cut results are claimed here. The adapter implements the existing `SlotIO` interface in `../include/orcsdr_radio_discovery/library.hpp`. It does not replace the F0b journal or migrate P25 profiles.

## Storage contract and isolation

After the **caller successfully mounts an SD card** at `/sdcard` with an ESP-IDF FAT VFS, `MicroSdSlotIO::begin("/sdcard")` provisions only these FAT 8.3-compatible paths:

```
/sdcard/orcsdr/disc1/obs1a.bin
/sdcard/orcsdr/disc1/obs1b.bin
```

No caller-supplied filename or directory suffix is permitted. The mount path must be absolute, non-root, bounded to 95 characters, must not include dot segments, doubled separators, invalid characters, or a `p25`/`orcsdr` component. On POSIX hosts, intermediate symlink mount paths and symlink directory entries are rejected; leaf opening uses `O_NOFOLLOW` where supported. FAT does not support symlinks. No rename/unlink/format is performed by this adapter. The **only** filesystem changes are creating the dedicated discovery directories and truncating/writing the exact selected slot. In particular, it never reads, writes, renames or deletes `P25.cfg`, `/orcsdr/p25/*`, or any existing profile.

`MicroSdSlotIO` contains fixed-size path arrays and uses no C++ heap allocations. `max_snapshot_bytes` must be set to `Library<RF,DIR,NOTE>::max_wire_bytes` so all reads and writes are bounded by the journal's existing capacity. It is **not thread-safe**: callers must serialize load/save/unmount on a dedicated storage task. The underlying ESP-IDF VFS/FatFs implementation can allocate internally; host tests do not establish a device-level heap or stack budget.

Each read opens only the requested file, checks regular-file type and its size against both destination capacity and configured maximum, fully reads its announced byte count, probes for concurrent growth, and closes. Only `ENOENT` with a verified, accessible parent directory is returned as `IoState::missing`. A missing SD mount, bad directory, oversize file, short read, permission error, failed close, or other failure is `IoState::error` so the journal **fails closed** rather than treating an unavailable card as an empty database. `actual` is zero on errors.

Each write opens **only the requested slot** with `O_TRUNC`, loops through partial writes and `EINTR`, then requires successful `fsync(fd)` and `close(fd)` before reporting success. Failure details are available through `last_error()` and `last_errno()`. The F0b `Journal::save` additionally reads back, length-checks, CRC-checks and decodes the inactive slot before advancing its active generation. It never overwrites the previously active slot during a save. If `fsync` or `close` fails, the write outcome is **uncertain** even though the method returns false; after remount, reload the journal before another save. Never assume an interrupted write was committed or discarded.

**Recovery boundaries:** a newer slot with a torn but readable length and invalid CRC is rejected by the F0b journal and the older valid generation is selected (`LoadState::recovered`). A file larger than the supplied buffer, an unreadable file, mount failure or OS I/O failure returns `IoState::error`, therefore `LoadState::io_error`, **not** automatic fallback. Two equally numbered generations yield `LoadState::ambiguous`. This adapter never deletes an invalid slot as a repair strategy. FAT32 power-cut atomicity, directory-entry durability and SD controller cache flush behavior remain **unverified until physical testing**; a host `fsync` result is not a power-loss guarantee.

## Host build and fault injection

From the OrcSDR repository root on a POSIX machine (Linux/macOS/WSL), with CMake 3.16+, C++17 and GCC or Clang:

```bash
cmake -S components/orcsdr_radio_discovery/esp_idf -B build-discovery-sd -DCMAKE_BUILD_TYPE=Release
cmake --build build-discovery-sd --parallel
ctest --test-dir build-discovery-sd --output-on-failure
```

Repeat under AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -S components/orcsdr_radio_discovery/esp_idf -B build-discovery-sd-asan \
  -DCMAKE_BUILD_TYPE=Debug -DORC_DISCOVERY_SD_SANITIZE=ON
cmake --build build-discovery-sd-asan --parallel
ctest --test-dir build-discovery-sd-asan --output-on-failure
```

`micro_sd_slot_io_tests` uses real host filesystem calls in temporary directories plus a fault-injectable VFS port. It tests two separate slot filenames, exact-limit and oversize reads, bounded destination buffers, partial reads/writes, `EINTR`, missing versus invalid mounts, read and write errors, short read/zero write, rejected mounts and symlinks, directory-create errors, sync and close failures, no P25 profile mutation, F0b `Journal` restart/recovery after CRC damage and partial writes, and explicit reload after an uncertain sync outcome. It does **not** use ESP hardware. Run this suite against the **real parent `library.hpp`** in the OrcSDR tree; the test must not be run against a substituted journal API.

## ESP-IDF opt-in integration — no firmware edits included

1. Have the existing SD owner mount the card and check the result from `esp_vfs_fat_sdmmc_mount()` or `esp_vfs_fat_sdspi_mount()` (whichever matches the device). Use mount root `/sdcard`, `format_if_mount_failed = false` and a sufficient `max_files`; never autoformat a user's SD card. If the existing application already mounted it, **reuse that mount** rather than mounting twice. These mount helpers are external to this adapter.
2. Add **only this** `esp_idf` directory to the consumer's `EXTRA_COMPONENT_DIRS` if integrating in a separate, authorized firmware task. Its local `CMakeLists.txt` declares an opt-in ESP-IDF component and uses the existing parent `include` directory. No changes to OrcSDR's shared CMake or `main.cpp` are included in F0c.
3. Construct the adapter after mount; use the same configured library capacities in `Journal`, `Workspace`, and the adapter. Example (for illustration; not executed on a device):

```cpp
#include "orcsdr_radio_discovery/library.hpp"
#include "orcsdr_radio_discovery/esp_idf/microsd_slot_io.hpp"
namespace rd = orcsdr::radio_discovery;
namespace sd = orcsdr::radio_discovery::esp_idf;
using Database = rd::Library<16, 16, 16>;
using DiscoveryJournal = rd::Journal<16, 16, 16>;

// Allocate these in a suitably reserved storage arena/PSRAM, NOT on a
// small RTOS task stack. Measure real peak RAM before device approval.
static Database database;
static DiscoveryJournal journal;
static DiscoveryJournal::Workspace workspace;
static sd::NativeFilePort fs;
static sd::MicroSdSlotIO slot_io(fs, Database::max_wire_bytes);

// Called by the serialized storage task after a confirmed SD mount:
if (!slot_io.begin("/sdcard")) {
    // log last_error() and last_errno(); refuse to load/save
} else {
    const auto state = journal.load(slot_io, database, workspace);
    // empty: new/confirmed volume only; recovered: alert and preserve
    // corrupt/ambiguous/io_error: do not save over untrusted state
}

// Later, only after valid database updates and appropriate consent:
// const auto result = journal.save(slot_io, database, workspace);
// handle result; if io_error, remount/reload before further saves.
// On unmount or media removal: slot_io.end(); then SD unmount.
```

4. Do not call journal read/write on the DSP, SDR USB, real-time decode or UI render thread. Keep one storage owner with a mutex or task queue, serialize card unmount with journal I/O, and preserve `database` on any non-successful load. If external P25 configurations exist, leave their own persistence code unchanged. F0c is **only** a new discovery snapshot.
5. On the ESP32-P4 hardware acceptance run, test fresh card, pre-existing legacy profile/card, removed/read-only/too-full media, forced short writes, card removal during write, `fsync` failure, reset/power interruption for both slots, restart selected generation and data integrity. Record SDK version, board, card model/filesystem, timing, internal and PSRAM usage, free heap, error logs and physical results separately from host results. Do **not** claim power-cut safety before those results exist.

**No dependency on REA competitor code:** this adapter implements a filesystem boundary for OrcSDR's own journal. It reads no protocol source or private IQ fixtures.
