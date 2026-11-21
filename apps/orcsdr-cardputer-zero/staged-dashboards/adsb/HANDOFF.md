# ADS-B staged-dashboard implementation handoff

**Assignment:** `apps/orcsdr-cardputer-zero/staged-dashboards/adsb/` only.

**Local working lane:** `sandbox/cardputer-zero-adsb-dashboard`.

**Fetched remote baseline:** `codex/cardputer-zero-ui-audio-controls` at `73736f9e784dd8fcc20c935620740a00dcbca301` (2026-11-17). That exact SHA is provenance; the integration branch can advance. No coordinator Home/FM, selector, palette or audio files were changed.

## File inventory

- `adsb_module.hpp`, `adsb_module.cpp`: 32-ICAO bounded model, per-aircraft field retention, expiry/selection/scroll, CPR timing/generation, single-host radio adapter, native shared decoder callback/counters/continuity.
- `adsb_renderer.hpp`, `adsb_renderer.cpp`: headless 320×170 RGB565 list/detail/empty UI, dirty widget/row paint, exact pixel file export.
- `tests/adsb_tests.cpp`: explicit native decoder frames, bad CRC, 2.56 MS/s mismatch, partial state, capacity/eviction, navigation/aging, same/cross/stale/old-generation CPR, CU8 split/gap, lifecycle errors, pixel/regression checks.
- `preview.cpp`: explicit DEMO list/detail/empty PPM generator.
- `CMakeLists.txt`: isolated C++17 build of the new code **and the existing portable ADS-B decoder**.
- `README.md`, `INTEGRATION.md`, `RESEARCH.md`, this handoff.

## Evidence and commands

From repo root:

```sh
cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/adsb -B build/staged-adsb -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/staged-adsb --parallel 2
ctest --test-dir build/staged-adsb --output-on-failure
build/staged-adsb/orc_zero_adsb_preview --demo --screenshot build/staged-adsb/adsb-demo.ppm
build/staged-adsb/orc_zero_adsb_preview --demo --empty --screenshot build/staged-adsb/adsb-empty.ppm
build/staged-adsb/orc_zero_adsb_preview --demo --detail --screenshot build/staged-adsb/adsb-detail.ppm
git diff --check -- apps/orcsdr-cardputer-zero/staged-dashboards/adsb/
```

**Sandbox constraint:** direct GitHub TCP/DNS and full clone were unavailable. The development harness used the exact scoped header/interface plus a **temporary reconstructed local decoder translation unit** matching the inspected implementation; this temporary mirror is outside the assigned folder and **not part of the deliverable**. The coordinator should rerun the standalone commands against the **real shared source on a complete checkout** before claiming official integration acceptance. Tests reported `1/1 passed` on Debian GNU C++ 14.2 / CMake 3.31.6 (headless x86_64); standalone demo, empty and detail previews each rendered 320×170 with clear DEMO labeling and no observed text collision. No claim of measured Cardputer CPU, RAM, RF or LCD performance.

**Source code delivery commit:** reported separately in final completion message if a verified commit and remote publication are possible. Otherwise the folder-scoped patch/artifact is the handoff. Do not merge to main, flash or contact the Pi.

## Memory and overflow policy

- Native decoder: fixed 16,640×uint16_t magnitude samples (~32.5 KiB); each host chunk ≤4,096 complex samples. No per-sample allocations.
- Aircraft: fixed 32 entries, including 2 recent CPR reports per ICAO; LRU by oldest `last_seen_ns`, tie by smaller ICAO. Monotonic 90 s TTL. Only same-ICAO, same-generation even+odd CPR separated by ≤10 s yields a position.
- Unique ledger: at most 256 24-bit ICAO IDs stored as uint32_t (~1 KiB); when exceeded `unique_capped` flags that count is a lower bound. Session counters are separate from active list.
- Snapshot: 32 fixed aircraft; renderer: 320×170×RGB565 = 108,800 bytes plus one cached snapshot. No history buffers, dynamic table growth, unbounded logs, DB, map or plot.
- CU8: accepted even byte lengths up to 262,144, split internally into ≤8,192-byte chunks; queue storage is owned/bounded by host. Input metadata with incorrect center/rate/generation/timestamps or odd length is rejected explicitly.
- Old-generation IQ is rejected; receiver loss clears CPR pair validity. Missing fields are never filled with synthetic zero.

## Coordinator physical acceptance checklist

1. Integrate selector + `Controller` on the **single existing receiver owner**; verify 1090000000 / 2048000 actual readbacks, continuity and generation rollover.
2. Run the scoped CMake/CTest commands against the actual GitHub shared decoder, run the three previews, inspect `full_clears` on repeated state revisions.
3. With the appropriate 1090 MHz antenna, capture on-air frames and verify ICAO/callsign/altitude/age against a validated receiver; zero aircraft on a mismatched antenna does not establish a regression.
4. Operate physical keyboard: Up/down, Enter, Escape; ensure global Space/P/Tab/G/Q are intact and FM volume/audio/palette settings remain untouched.
5. Measure physical LCD readability/flicker, SDR throughput/USB overruns, long-running CPU/RAM and exit/retune/reconnect behavior on the actual Cardputer. Report these separately from host preview evidence.

**Material remaining risk:** display/receiver integration is not present in the remote production selector yet, and the exact-rate native decoder must be supplied with actual continuous CU8 from the central host. No physical SDR antenna, Linux Cardputer LCD, ALSA or Pi access was exercised in this sandbox.
