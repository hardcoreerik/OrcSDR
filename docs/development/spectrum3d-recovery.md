# Spectrum3D recovery evidence — 2026-09-08

> **Paused checkpoint:** [hardware results and resume notes](spectrum3d-pause-checkpoint.md) supersede the untested-hardware statements in this earlier offline report.
## 1. Executive finding
The exact committed pre-rebase source is recoverable. It is not established that it was the firmware the user observed as unusually smooth. Git patch equivalence disproves a dropped-renderer-patch explanation for the rebase itself. Later uncommitted experiments changed geometry and mode and introduced a blank-wire adapter defect. Mainline also increased static memory and changed the RTL-SDR dependency; these are independent variables, not proven frame-rate causes.

## 2. Git timeline
| State | Commit |
|---|---|
| Original base | a94d237 |
| Original renderer | 9cd2790bc15e285bdd50ff9de04c5a05a4ee63fb |
| Pre-rebase B | c150c1eaaea2cc084230e23a3baea3edd37c5f2e |
| Main A | 376230ab9aa49c7abaf36c6c42e7b5a1dde41140 |
| Replayed renderer | b5d2f35b71dde7eb58840ff185b63702dcbc26bb |
| Post-rebase C | 394354d33300d1a9bbea211438902b65705d8f20 |
| Recovery D | uncommitted changes on A, identified by build manifest digest |

## 3. Candidate ranking
1. B: strongest recoverable source candidate for the original implementation. Renderer plus attribution amendment; exact source, no exact physical firmware association.
2. C: exact replayed source with current integration baseline. Same renderer patch, different complete program/dependencies.
3. Preserved dirty worktree: later experimental source, known materially different geometry and wire mode, unsuitable as original fast candidate.
4. Archived fullscreen worktree/baseline: contextual predecessors; no additional Spectrum3D history implementation found in relevant history. Existing stash contains documentation only, not a hidden renderer.
No unresolved missing source candidate justifies unreachable-object scanning at this point. No fsck/GC/prune was run.

## 4. Replay evidence
`git range-diff a94d237..c150c1e 376230a..394354d` marks both commits `=`. Renderer and attribution patch survived. Reflogs identify rebase picks, not a lost conflict resolution. Patch equivalence does not imply equal binaries, saved controls, live radio workload, or observed firmware.

## 5. Mainline changes
POCSAG added dashboard/decoder/store objects, dispatch, radio session and navigation integration, and IQ processing changes. `rf_analysis.cpp` and renderer control/geometry source have no upstream change in this interval. Analysis remains best-effort Core 1 idle priority, with 16/33/100 ms configured intervals, distinct from presentation. The active visualizer loop returns before the ordinary dashboard delay. There is no evidence that the final dashboard delay caps Spectrum3D.

## 6. Build configuration differences
RTL-SDR changes from v0.7.9 / 662fdcf to v0.7.14 / 69e6184. mDNS lock changes 1.11.3 to 1.12.0. Hosted stays 3.0.6. Preserve these versions in historical builds; recovery retains current pins. Tracked renderer patch changes M5GFX from two to three DSI buffers. Build policy prose previously said two and is corrected in D.

## 7. Historical artifacts
Evidence directory: `F:/Ai/OrcSDR-Temp/spectrum3d-recovery-evidence-20260908`.
`historical-build-hashes.csv` records paths, SHA256, size and modification time; ELF/BIN/MAP/sdkconfig copies are preserved under `historical-build`. Original untracked artifacts remain in place and are inventoried. Binary-capable tracked diff, relevant reflog, worktrees, remotes, stashes and range comparison are retained. Dates alone do not prove revision. Preserved artifacts are not asserted to be the original physically observed fast firmware.

## 8. Visual differences
B/C default to filled surfaces with full history sampling and native or 2x raster. Dirty experiments changed Balanced to 4x, Eco to 6x, limited visible history to approximately eight/six rows, and defaulted to wire mode. Those are workload reductions, not evidence of an optimized equivalent surface. A wire endpoint compared scaled screen X against raster X, suppressing the trace. The recovery retains the filled path and uses an explicit row-batched adapter; scaled coordinates are applied only at emission and isolated ridge cells preserve steep peaks and gaps.

## 9. Speed explanation boundary
Historical surface telemetry reported 12.7 submissions/s; later blank-wire runs reported about 35–37 and ~25 ms frame work, while VSYNC was ~66 Hz. These are not 60 distinct frames/s. Nonzero span counts contradict the earlier explanation that no geometry existed. Existing combined timings cannot establish clear, geometry or drawing as the bottleneck. The ~61 ms capture from peakavg is not a Spectrum3D benchmark. A serial timeout expecting OPEN_REQUEST instead of external RTL_VIS_OK did not establish a lockup.

## 10. Recovery strategy
Retain the patch-equivalent original architecture on current main, separately identify dirty experiments, restore useful filled geometry, fix demonstrated memory/adapter defects, and add measurements before choosing a different compositor. Do not change radio priority, C6, DMA strategy or dependencies without evidence.

## 11. Files changed
Recovered renderer patch: rf_visualizer.cpp/.hpp, rf_visualizer_controls.cpp, spectrum_history.hpp, main.cpp scheduling, tracked M5GFX patch, historical report and history test runner. Recovery additions: row drawing adapter and tests/benchmark, PSRAM timing storage, diagnostic commands, build manifest tool, CMake/wrapper integration, build-policy correction and this report. No radio/DSP implementation or dependency pins changed in D.

## 12. Architecture
The producer reduces FFT bins and time slices by maxima into a 128-row bounded history. A coherent snapshot is copied under the history mutex, then drawing occurs outside the lock. Timestamp age controls perspective and the final 28% smooth fade. Presentation targets 16,666 us independently of new FFT revisions. Full uses 1196x590; Balanced/Eco retain 598x295 with 128/64 effective frequency buckets and every history row. The current trace is cyan; darker blue surface bodies and brighter ridges increase separation. No rows are silently skipped.

## 13. Performance work and instrumentation
Confirmed boot-memory fix moves 512-byte timing samples to PSRAM. One reusable row of spans in PSRAM lets projection and actual M5GFX drawing be timed separately, without a timestamp per primitive. Timings now separate snapshot/history, clear/grid, geometry/raster, drawing, overlays, submission and buffer wait. `RTL_VIS PERF CLOCK` measures 256 timer calls on demand; actual hardware instrumentation overhead is still unmeasured. Geometry timing includes remaining setup and per-row bookkeeping. No DMA or speculative compositor replacement was introduced. No new device speedup is claimed.

## 14. Synchronization
Installed IDF 5.5.4 `esp_lcd_panel_dpi.c` writes back cache for driver-owned buffers before setting cur_fb_index. The DMA completion handler selects that index for the next linked transfer. VSYNC callback receives no buffer identity. D retains three buffers and two-refresh retirement, exposes `completion_identity=unavailable`, and does not equate VSYNC with unique completed frames. Exit waits for retirement before copying to FB0. This is source-reviewed behavior, not proof of tear-free scanout or safe operation during hardware underruns. Hardware validation remains a release blocker.

## 15. Measurements
Identical synthetic workload, 128 history rows, 128 buckets, 598x295 Surface; 30 warmup frames and 150 measured frames, host g++ -O2:

| Candidate | Mean us | p95 us | p99 us | Emitted spans |
|---|---:|---:|---:|---:|
| B | 407 | 458 | 659 | 2,863,665 |
| C | 410 | 506 | 530 | 2,863,665 |
| D | 408 | 504 | 557 | 2,863,665 |

B/C output checksums match (49414); D changes palette (50577). This benchmark measures geometry and a common host drawing sink, not M5GFX, PSRAM bandwidth or device FPS. D's separate actual-adapter test measured 1915 us optimized and 7572 us sanitized, including host allocation/test overhead; it is not directly comparable with that benchmark. These results provide no evidence of a geometry regression from rebase and no proof of 60 FPS on Tab5.

## 16. Memory
| Candidate | BIN bytes | Internal BSS bytes |
|---|---:|---:|
| A | 2,302,272 | 175,756 |
| B | 2,275,232 | 175,652 |
| C | 2,311,264 | 176,404 |
| D | 2,314,144 | 175,932 |

D removes 472 bytes of internal BSS versus C, but remains 176 bytes above A. Reserve remains 40960 and ALWAYSINTERNAL 8192. All app images fit the 4 MiB partition. Row=260 bytes; host History=33584, Snapshot=33560. D additionally allocates one 1196-span row and 128 timing samples in PSRAM. Allocation failure displays memory-unavailable; missing timing storage reports zero samples. Build/link success cannot prove the early DMA pool reservation succeeds at boot.

## 17. Tests
All executed successfully:
- B/C: `wsl bash ./tools/test-spectrum-history.sh` — optimized and ASan/UBSan PASS, logs tests-b.log/tests-c.log.
- D: `wsl bash ./tools/test-spectrum-history.sh /mnt/f/Ai/OrcSDR-Temp/spectrum3d-recovery-evidence-20260908/recovery-terrain.ppm` — optimized and ASan/UBSan PASS, final log tests-d-controls.log. Covers empty/full/wrapped history, uneven timestamps, unsigned clock wrap, expiration, reconfiguration/tuning reset, frequency/time peak preservation, camera extremes, frame-retirement arithmetic, actual production adapter, nonblank modes/scales, narrow carrier, ramp, moving peaks, frozen-time stability and resumed-time changes. Existing control self-check also passes. Freeze/UI transition behavior itself is a hardware gate, not established by the timestamp unit test.
- `wsl bash ./tools/test-radio-scan.sh` — optimized and sanitizer PASS; tests-radio.log.
- `git diff --check` — PASS.
- Benchmark: `wsl g++ -std=c++17 -O2 -Wall -Wextra -Werror -I<CANDIDATE>/apps/orcsdr-tab5/ui <D>/tests/spectrum_history_benchmark.cpp -o <EVIDENCE>/benchmark-<candidate>` then execute each B/C/D binary; raw replies retained.
No full repository suite, device regression or serial command was run.

## 18. Native builds and provenance
Each candidate used a fresh worktree and `apps/orcsdr-tab5/tools/build-tab5-idf.ps1`, native ESP-IDF 5.5.4, with its committed dependency lock. A/B/C/D all PASS. D was rebuilt once after final diagnostics and manifest edits; build-d-final.log is authoritative. A/B/C logs are build-a.log/build-b.log/build-c.log. No C6 image was supplied and no dependency was downgraded to run hardware. Reverse-apply checks confirm each candidate's tracked M5GFX patch is installed. B/C effective sdkconfig differs only by `CONFIG_MDNS_ENABLE_BROWSE=y` in C.
`candidate-manifest.json` contains full SHAs, artifact SHA256/size, memory sections and patch verification. `recovery-build-manifest.json` also contains source/config/dependency content hashes, build mode, IDF revision and driver hash, compiler command-file hash and ELF/BIN hashes. Every recorded D input was rehashed after the final build: no mismatches. D is intentionally dirty and identified by digest, not the base SHA alone.

## 19. Comparison matrix
| Gate | A main | B pre-rebase | C post-rebase | D recovery |
|---|---|---|---|---|
| Fresh native build | PASS | PASS | PASS | PASS |
| History tests | not applicable | PASS | PASS | PASS + production adapter |
| Renderer patch | old main | original | equivalent to B | restored + focused fixes |
| Geometry benchmark | different renderer, not comparable | recorded | recorded | recorded |
| Physical original identity | unknown | unproven | unproven | not flashed |
| 59+ submissions/s | unmeasured | unmeasured | not established | unmeasured |
| Boot, audio, tearing, touch | not tested this run | not tested | historical boot failure | not tested |

## 20. Future hardware A/B procedure — prepared only
Separate flash authorization required. Preserve NVS and recovery image. Verify BIN hash and boot firmware identity for each candidate; D additionally returns `RTL_VIS PERF ID`. Use the same RF source at 96.1 MHz or another agreed stable input, 960 kS/s, identical gain/audio settings and antenna path. Record actual values rather than assuming saved controls.
After `RTL_VIS OPEN spectrum3d`, expect external `RTL_VIS_OK`. Explicitly set visual.quality=1, spectrum3d.slices=6 (128 rows), history_s=20, elevation_deg=32, azimuth_deg=20, zoom=1, depth_scale=1, z_gain=1, mesh_mode=1, color_mode=2, line_decimation=0, auto_orbit=0, visual.freeze=0 using `RTL_VIS SET <id> <value>`. Record GET replies; A's renderer and unavailable diagnostics must be marked as workload differences. Save and later restore the user's prior controls.
Collect 60 seconds RF baseline, warm 30 seconds, reset PERF and capture five minutes at one-second intervals: PERF, PERF TIMING, STATUS, device health and RF counters; D adds PERF SOURCE and PERF ID. Preserve raw responses alongside candidate identity. Use PERF CLOCK once to quantify timer cost; per-row timestamps contribute bounded but nonzero overhead. Counters in PERF SOURCE are cumulative accepted input/analysis counters; calculate deltas over elapsed time.
Exercise freeze/resume, source loss, camera/quality controls, overlays and entry/exit. Best candidate gets 30-minute soak. Corroborate distinct frames/tearing with physical observation or high-frame-rate video. Target >=59 submissions/s, mean/p95 work <16.67 ms, p99 <20 ms, misses <1%, zero submission errors, stable memory, responsive touch and no material RF/audio regression. Visual acceptance is separate. If B or C fails early boot, record that failure and stop that candidate rather than repeatedly reflashing or altering its source identity.

## 21. Remaining issues, ranked
1. BLOCKER for 60 FPS acceptance: no D hardware measurements; dominant device stage remains unknown. The plan's measured optimization step stops here pending separately authorized hardware evidence.
2. BLOCKER for release: boot DMA headroom and physical buffer ownership/tearing are unverified in D despite reduced BSS.
3. FIX BEFORE MERGE: evaluate source cadence with accepted-IQ and analysis deltas under identical RF conditions; no justified task-priority change yet.
4. FIX BEFORE MERGE: verify visual quality, freeze/source-loss and overlays on device; synthetic preview is not physical acceptance.
5. Historical uncertainty: exact firmware observed during the earlier fast demonstration remains unproven; source recovery alone cannot resolve it.
6. Measurement limitation: p95/p99 describe the last 128 frames, cumulative stage means cover the measurement window, and normal per-row instrumentation overhead needs a device measurement. Completion identity is unavailable.

No flash, serial/device access, commit, push, PR, merge, force push, cleanup, stash changes or main edits were performed. New local recovery refs and isolated comparison worktrees are retained. The original dirty experimental worktree remains untouched.
