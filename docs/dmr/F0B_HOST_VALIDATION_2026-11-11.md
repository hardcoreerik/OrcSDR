# F0b — Host discovery storage verification (2026-11-11)

**Status:** host-only storage prototype implemented and verified. **Not** ESP32-P4, SD-card hardware, user profile migration, or on-air DMR acceptance.

## Exact scope

- `components/orcsdr_radio_discovery/include/orcsdr_radio_discovery/library.hpp` — bounded `Library<RF,DIR,NOTE>`, separate RF observations/directory candidates/user annotations, identity+known-field matching, explicit conflict handling and separate retention of ambiguous observations.
- Versioned endian-explicit `ORAD` v1 snapshots with lengths, generation, record counts and CRC-32 for accidental corruption detection.
- Two inactive/active storage slots with read-back verification, recovery of older valid revision after a torn/corrupt write, and fail-closed handling of I/O errors and ambiguous equal generations.
- `tests/storage_tests.cpp` — simulated short write/corruption/reboot and a POSIX host file-backed persistence test using `fflush`, `fsync` and reopened files.
- `components/orcsdr_radio_discovery/CMakeLists.txt` — strict host C++17 compilation and two CTest suites.

## Reproduction

On the host used for this checkpoint: Linux, GCC 14.2.0, CMake, CTest.

```bash
cmake -S components/orcsdr_radio_discovery -B /tmp/orcsdr-discovery-rel -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/orcsdr-discovery-rel --parallel
ctest --test-dir /tmp/orcsdr-discovery-rel --output-on-failure

cmake -S components/orcsdr_radio_discovery -B /tmp/orcsdr-discovery-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/orcsdr-discovery-asan --parallel
ctest --test-dir /tmp/orcsdr-discovery-asan --output-on-failure
```

**Observed local results:** Debug **2/2 PASS**, Release **2/2 PASS**, ASan/UBSan **2/2 PASS**. Earlier failing assertions in the implementation session were corrected and the complete suites rerun after the final changes. No native firmware compile or physical SD-card stress/fault-injection was performed.

### Host footprint (GCC x86-64 only)

| Object | `sizeof` on test host |
|---|---:|
| `Observation` | 72 bytes |
| `Library<3,3,3>` | 1,976 bytes |
| `Journal<3,3,3>::Workspace` | 5,696 bytes |
| `Library<16,16,16>` | 10,400 bytes |
| `Journal<16,16,16>::Workspace` | 29,912 bytes |

**Important:** these figures are ABI-specific, **not** ESP32-P4 heap/stack measurements. The workspace must be allocated outside time-critical/limited task stacks. Capacity selection, PSRAM placement and SD I/O scheduling require device measurement.

## Provenance and non-goals

Uploaded GitHub source file blobs for `library.hpp`, `storage_tests.cpp`, `README.md` and `CMakeLists.txt` were independently checked against `git hash-object` of the compiled local files.

The two-slot journal relies on a future `SlotIO` backend to *actually guarantee durable write+flush+close* on the target storage system. Read-back and CRC do not establish power-loss atomicity of an unqualified FAT/microSD card. A host POSIX `fsync` test proves host file persistence in an ordinary restart, not physical power-cut resilience.

This commit deliberately does **not** modify `main.cpp`, production P25 code or existing `/orcsdr/p25/**` profiles. The `tools/radio_profile_import` preview remains a different module and is **not** wired into `Library`; the current model cannot automatically apply a plan.

## Remaining F0 gates

1. **F0c:** bounded, failure-reporting ESP-IDF SD `SlotIO` adapter and actual on-target power-interruption/recovery checks.
2. Preserve existing P25 v2 profile behavior while designing its non-destructive migration view; no reindexing or truncation.
3. Integration tests connecting `radio_profile_import` preview output to a guarded, explicit commit, while keeping data sources distinct from RF observations.
4. D1/D2: feed real validated P25/DMR/NXDN decode events, then implement scan, history and UI without requiring a network.

Tracker: [TRACKING.md](TRACKING.md). Workstreams: [WORKSTREAMS.md](WORKSTREAMS.md).
