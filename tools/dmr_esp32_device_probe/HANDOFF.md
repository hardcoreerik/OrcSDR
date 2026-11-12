# Agent 3 — Phase D1-H handoff

Owned scope: **`tools/dmr_esp32_device_probe/**` only**, read-only external
D0/D1, Tab5 and P25 sources.

- Producer/consumer: `include/orcsdr_dmr_probe/handoff.hpp` is fixed-size,
  32-bit lock-free atomic SPSC; 8 x 240 complex IQ at 48k, at most 960 per
  atomic offer; full queue counts losses and skips sequence to reset the real
  D1 receiver on next accepted block. Producer can also `note_gap()` or
  increment epoch on retune/reconnect. No heap/RTOS/I/O in producer.
- Real DMR integration: `CMakeLists.txt`, `src/probe_host.cpp`,
  `src/pattern.cpp`, `include/orcsdr_dmr_probe/pattern.hpp` reference actual
  D0 + D1 frontend + D1 protocol + D1-C integration bridge. No clone or new
  decoder. Synthetic 48 kS/s frequency steps include ETSI BS sync and Golay
  type but are *not* CRC-validated RF captures.
- Physical build: isolated `esp_idf/` project, no production app changes, no
  automatic flash. Measures 5 ms IQ worker latency, bounded queue, losses,
  PSRAM/internal heap, object sizes and worker stack high-water. Deliberate
  overload and epoch/gap injection are supported. Capture logs via host `tee`
  or PowerShell `Tee-Object`, never SD.
- Safety tests: `tests/test_handoff.cpp`, seven deterministic groups, Release
  and ASan/UBSan PASS on this host; see `RESULTS.md`. Full D1-linked host and
  physical ESP32-P4 builds/tests are **NOT RUN** here.
- Missing production seam: no presently verified shared 48 kS/s DMR complex
  IQ tap; current wideband CU8->240k DSP multirate is a lab, P25 private
  channel IQ is not shared. See `INTEGRATION_CONTRACT.md`. Do not wire DMR
  to an audio PCM path or add a second USB/device owner.
- Signoff: run the actual linked DMR host benchmark and standalone ESP-IDF
  probe on a dedicated Tab5; collect logs and measure real USB load and
  48k DDC only after separately approved source integration.

All source, scripts, docs and fixtures are within this owned directory.
No firmware, D1, USB, central CMake, M5GFX, P25 or `main` changes.
