# OrcSDR Radio Discovery — F0b host storage foundation

**Status (2026-11-11):** host-built portable evidence library and two-slot local snapshot journal. NOT wired to Tab5 SD, P25 v2 profile migration, the radio/scan engine, M5GFX, provider import, the DMR decoder, or real-IQ replay. No RF reception is claimed.

## Scope

- `evidence.hpp` (F0a): typed proof levels, receiver (not transmitter) coordinates, first-party RF observations versus directory/user origins, bounded in-RAM event ring.
- `library.hpp` (F0b): bounded fixed-memory `Library<RF,DIR,NOTE>` with **three separate collections**: receiver RF records (`first`, `last`, encounter count, strongest proof), directory candidate records (named source, URL, license and optional SHA-256/retrieval UTC), and user annotation records keyed to a saved record ID. No user edit silently modifies an imported record or an RF observation.
- Distinct system/talkgroup/site identities are keyed by **protocol, receive frequency, explicit known-field flags and values**; filename, display label, sample energy and source URL cannot establish RF identity. Missing identity information may be `ambiguous` and must be reviewed rather than merged. Same frequency can legitimately carry separate sites and time slots. Directory source conflicts require explicit `keep_both`; annotations require explicit `replace`.
- `Journal<RF,DIR,NOTE>` serializes an **explicit little-endian v1** snapshot (magic `ORAD`, version, generation, counts, total length, next ID, CRC-32, records). The checksum covers the header except its checksum bytes plus the payload. It is for accidental corruption detection, not authentication: only the existing signed catalog may establish trusted distribution integrity.
- **Two fixed slots**: write and flush the inactive slot, read back and validate before changing the active generation. On restart, choose the newest valid generation. If the newer copy is torn/corrupt, restore the older valid slot and report `recovered`. If both are bad, ambiguous, or I/O fails, fail closed and leave the caller's live database unchanged. No file is deleted on failure.

## Filesystem adapter contract

`SlotIO` has `read(slot, destination, capacity, actual)` and `write_durable(slot, bytes, length)`. An ESP-IDF adapter (NOT yet implemented) must use **two dedicated, versioned SD filenames**, never the existing `/orcsdr/P25.cfg` or `/orcsdr/p25/...` files. `write_durable` must truncate/write **only the requested slot**, check short writes, `flush`/sync to its medium, close, and report all failures; `read` must distinguish missing, corruption (bytes returned), and I/O error, and reject oversize snapshots. SD/FAT power-loss atomicity is **not established** by host tests and requires device fault-injection before a release claim. Keep the adapter's read/write execution off the real-time DSP/USB/UI path. Allocate `Journal::Workspace` in suitably reserved memory (typically PSRAM), **not on the ESP32-P4's task stack**, and measure the actual footprint.

The format is a standalone *discovery snapshot*, **not** the OrcSDR P25 v2 profile format, not the agent's portable JSON import format and not a P25/DMR/NXDN on-air packet. The input adapter must validate the user's preview plan against a fresh destination snapshot and explicit permissions before writing; this foundation intentionally cannot apply that plan yet.

## Reproduce host tests

From OrcSDR repository root:

```bash
cmake -S components/orcsdr_radio_discovery -B build-radio-discovery -DCMAKE_BUILD_TYPE=Release
cmake --build build-radio-discovery --parallel
ctest --test-dir build-radio-discovery --output-on-failure
```

Optional Linux ASan/UBSan:

```bash
cmake -S components/orcsdr_radio_discovery -B build-radio-discovery-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-radio-discovery-asan --parallel
ctest --test-dir build-radio-discovery-asan --output-on-failure
```

`radio_discovery_evidence_tests` verifies the original F0a validity/ring semantics. `radio_discovery_storage_tests` checks identity and ambiguity, known-field correctness, first/last UTC ordering, no source overwrite, review/replace, bounds, byte-level serialization, version and CRC rejection, two-slot restart and recovery, torn writes, duplicate generations, missing-versus-corrupt, no overwrite on failure, and a **real POSIX file-backed restart with `fflush`/`fsync`** on Unix hosts. Neither suite executes a DMR decoder or a Tab5 SD filesystem.

## Next integration milestones

1. Add the bounded ESP-IDF `SlotIO` adapter and safe provisioning path; measure memory/latency on a real ESP32-P4 plus SD power-interruption scenarios (F0c/D2).
2. Design a compatibility-preserving migration *view* of existing P25 v2 profiles and the host import-preview schema; do not overwrite or truncate legacy P25 data (CFG-02).
3. Wire verified DMR/P25/NXDN events through the one-receiver owner into this host-tested model; implement scan/record/archive UI and controlled local web export only after receiver acceptance (D1/D2).
