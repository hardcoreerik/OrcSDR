# Spectrum3D engineering report

> Historical implementation report. See [recovery evidence](spectrum3d-recovery.md) for current validation. Sustained 60 FPS has not been established on hardware.

Worktree: `F:/Ai/OrcSDR-Temp/OrcSDR-spectrum3d-history-60fps`.
Branch: `codex/spectrum3d-history-60fps`, based on `origin/main` at `a94d237`.

## 1. Executive result

Spectrum3D now has independent display scheduling, timestamped bounded history,
peak-preserving reduction, an occluded blue/cyan height-field surface, working
camera controls, and serial performance diagnostics. Scheduling targets 60 Hz.
**Sustained 60 FPS and tear-free physical scanout are not hardware-proven.**
After implementation, the user authorized app-only flashing and a branch commit. No PR or merge was performed.

## 2. Root causes discovered

- Explicit 66/100/125 ms Spectrum3D draw intervals limited it to about 15/10/8 Hz.
- Drawing required a new FFT revision, coupling animation to measurement rate.
- Two display buffers were exchanged after a blind 20 ms delay.
- History indices were spread across depth regardless of elapsed time or fill level.
- A frame copied individual history rows under separate locks, rather than one coherent snapshot.
- Frequency decimation skipped bins; a narrow carrier could disappear.
- Draw order allowed older lines to draw over foreground geometry; there were no surfaces.
- Camera, time, color, and mesh controls were largely disconnected from rendering.
- Full-screen clears and static text were repeated; freeze could leave stale FPS telemetry.

## 3. Files changed

| Repository-relative path | Purpose and modifications |
| --- | --- |
| `apps/orcsdr-tab5/ui/spectrum_history.hpp` | Portable ring, reduction, live trace, projection, fade, horizon rasterizer, buffer retirement model. |
| `apps/orcsdr-tab5/ui/rf_visualizer.cpp` | Producer integration, coherent snapshots, M5GFX spans, controls, 60 Hz scheduling, ownership, telemetry, static chrome caching. |
| `apps/orcsdr-tab5/ui/rf_visualizer.hpp` | Exit reports failure if display ownership cannot safely be returned. |
| `apps/orcsdr-tab5/ui/rf_visualizer_controls.cpp` | Surface and hybrid blue/cyan become defaults; existing saved preferences remain intact. |
| `apps/orcsdr-tab5/tools/patches/m5gfx-tab5-pageflip.patch` | Existing M5GFX patch exposes three driver-owned framebuffers. |
| `apps/orcsdr-tab5/ui/main.cpp` | Honors failed visualizer exit; permits read-only performance queries. |
| `tests/spectrum_history_tests.cpp` | Deterministic history, peak, timestamp, raster, camera and ownership checks; optional PPM preview. |
| `tools/test-spectrum-history.sh` | Runs optimized and ASan/UBSan checks with temporary host binaries. |
| `docs/development/spectrum3d-renderer.md` | This report and future hardware procedure. |

## 4. Renderer architecture

```text
Existing RF analysis observer
  -> max-reduce FFT bins to 256 magnitudes
  -> latest measured trace + timestamped temporal peak buckets
  -> bounded history ring
  -> coherent UI snapshot under short, nonblocking lock
  -> elapsed-time depth + perspective + age fade
  -> front-to-back horizon spans
  -> M5GFX directly into a writable driver framebuffer
  -> DPI submission and refresh-event retirement
```

No replacement UI framework, FFT implementation, per-frame allocation, bitmap
animation, full-frame staging copy, or general-purpose 3D engine was added.

## 5. History behavior

Each row contains a 32-bit millisecond timestamp and 256 quantized magnitudes.
The ring has 128 slots with O(1) insertion and no shifting. A separate latest
measured trace distinguishes current RF from temporal peak hold.

History Time retains its 2–60 second range. Depth is unsigned elapsed milliseconds
divided by the selected duration: age zero is the front; age one is the back;
older rows are omitted. Clock wrap is covered by tests. Partial buffers occupy
only their actual time extent, and stalled input leaves real gaps.

To bound storage, each historical slice aggregates frequency-bucket maxima for
approximately `History Time / History Slices`; its timestamp is the first accepted
measurement in that bucket. At 20 seconds/64 slices, temporal resolution is about
313 ms. The separately drawn live trace still updates at the analysis rate.
This is temporal peak aggregation, not additional RF measurements.

Position moves between RF updates using elapsed time; no intermediate FFT values
are synthesized. Old geometry dims with age and smoothly loses height/brightness
over the final 28% of its lifetime. Freeze holds the displayed clock and stops
history acquisition; unfreezing resumes real elapsed time, so stale rows can expire.
Changing duration, slice count, center frequency, or RF span starts fresh history
on the next accepted measurement. This deliberately avoids mixing incompatible
time buckets or frequency axes. The visible duration changes immediately.

## 6. Spectrum fidelity

Every source bin belongs to a reduction bucket. The maximum finite value in the
bucket is retained; no source bins are skipped. Magnitudes use a fixed -160…0 dBFS
encoding, about 0.63 dB per step, so subsequent display-level changes apply uniformly
to historical data. Balanced/Radio Priority and Line Detail perform another max
reduction over adjacent stored buckets. They do not sample every second/fourth bin.

This preserves narrow carriers' presence and peak bucket values, while sacrificing
separation of carriers inside the same bucket. Min/max envelopes were not added;
the current representation preserves peaks but not within-bucket valley detail.

## 7. 3D surface implementation

The renderer projects a frequency/age height field into vertical screen spans.
Processing runs **front to back**, with a per-column horizon hiding already-covered
geometry. Visible spans form a filled terrain with darker blue faces and restrained
cyan ridges; the actual newest measurement is brighter than historical buckets.
Each terrain raster cell is written at most once after the bounded plot clear.

This is an oblique height-field projection, not arbitrary free-camera 3D. Elevation,
azimuth/shear, zoom, depth spacing, amplitude height, color mode, Lines/Surface/Points,
line detail, slow/medium orbit, and Reset Camera are connected. Camera extremes
are clipped to the plot. A subdued perspective grid follows the same projection.
Common floor/ceiling and auto-level settings apply to stored absolute magnitudes.
The common rainbow palette does not override the intentional blue/cyan treatment.
The pre-existing `visual.fps_overlay` remains unused; diagnostics are serial-only.

## 8. Performance changes

- Display cadence is independent of FFT revisions and uses a 16,666 µs deadline.
- Removed Spectrum3D's quality-dependent frame caps; quality controls real work.
- Max: native 1196×590 plot, up to 256 frequency buckets.
- Balanced: explicit 598×295 raster cells expanded 2×2, up to 128 frequency buckets.
- Radio Priority: the same raster size, up to 64 frequency buckets.
- User Line Detail may further reduce bucket count. All reductions preserve maxima.
- Slice count controls temporal complexity without the old hidden 16/24/32-row cap.
- Trigonometry is outside per-point loops. Projection constants are reused per frame.
- Horizon spans avoid a depth buffer, triangle sorting, and repeated hidden fills.
- Rendering writes directly to M5GFX canvases attached to driver buffers.
- Chrome is cached per buffer; only the plot is cleared during steady rendering.
  Active overlays conservatively invalidate chrome to prevent stale UI remnants.
- The producer never waits for the history lock; contention drops are counted.
- The UI can animate the previous coherent snapshot if a producer owns the lock.
- An unchanged full analysis snapshot is not recopied for each 3D animation frame.

The source analysis cadence and existing RF-pressure quality downgrade policy remain.
No acquisition or radio scheduling changes were introduced.

## 9. Display synchronization

The implementation was checked against installed M5GFX 0.2.27 `Panel_DSI.cpp/.hpp`
and ESP-IDF 5.5.4 `esp_lcd_panel_dpi.c`, `esp_lcd_mipi_dsi.h`, and the P4 HAL.
Driver-owned framebuffer submission performs cache writeback and updates the
buffer selected for scanout. `on_color_trans_done` is therefore **not** evidence
that the old scanout buffer is safe to overwrite.

Spectrum3D registers `esp_lcd_dpi_panel_register_event_callbacks` with
`on_refresh_done`; the ISR only increments a lock-free counter. The installed HAL
uses DMA frame completion on older P4 revisions, or bridge VSYNC on newer revisions.
Because the callback does not identify a buffer, a replaced buffer is retained for
two subsequent refresh events. Three buffers allow rendering to overlap this
retirement interval. No fixed 20 ms wait remains in the Spectrum3D steady path.
Other visualizer views retain their previous two-buffer timing path.

Touch/command repainting must acquire a writable buffer too. On exit, ownership is
retired before copying to dashboard FB0. If refresh events stall for 100 ms, exit
fails explicitly and retains ownership rather than recycling an unsafe buffer.

Reference: [ESP-IDF 5.5.4 MIPI DSI API](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html).
ESP-IDF is Apache-2.0; M5GFX is MIT. Existing notices remain. These sources informed
API use and synchronization; the history/rasterizer was implemented independently.
No external terrain code or datasets were copied.

## 10. Memory usage

| Allocation | Approximate size |
| --- | ---: |
| One history/live row | 260 bytes |
| 128-row history plus current trace/metadata | 33.6 KB |
| Coherent snapshot plus current trace/metadata | 33.6 KB |
| Horizon and reduced levels on render stack | Under 6 KB |
| Timing samples/counters | Under 1 KB |
| Each 1280×720 RGB565 framebuffer | 1,843,200 bytes |
| Three framebuffers | 5,529,600 bytes |

History and snapshot together are approximately 65.5 KiB. Host `sizeof` results are
33,584/33,560 bytes; P4's smaller `size_t` slightly reduces these. The third framebuffer
adds 1.76 MiB to the shared display allocation. A fresh Spectrum3D entry no longer
needs the old up-to-4-MiB shared RF history; history retained from another view can
still coexist until view-buffer cleanup/exit. Allocations are bounded and outside
the render loop. Partial allocation failure frees both new history allocations.

## 11. Automated tests

From the worktree, run:

```powershell
wsl.exe --cd F:/Ai/OrcSDR-Temp/OrcSDR-spectrum3d-history-60fps bash tools/test-spectrum-history.sh .local/spectrum3d/terrain.ppm
```

PASS with `g++ -std=c++17 -Wall -Wextra -Werror -O2` and separately with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`.
Checks cover empty/partial/full rings, capacity and order after wrap, timestamp
preservation and clock wrap, 0/half/full/expired time mapping, uneven arrivals,
temporal peak retention versus the actual live sample, bucket-boundary/end-bin
carriers, NaN rejection, duration/tuning reset, monotonic rolloff, display retirement
and counter wrap, motion without new measurements, eight camera/render controls,
clipping at control extremes, and no terrain-cell overdraw. The deterministic
preview includes moving narrow carriers and broad peaks with varying amplitude.
It uses the production rasterizer with a host pixel sink; it is not a Tab5 capture.

## 12. ESP-IDF build result

Command, from the worktree:

```powershell
./apps/orcsdr-tab5/tools/build-tab5-idf.ps1
```

**PASS**, ESP-IDF 5.5.4, native ESP32-P4 build, exit code 0. Final app binary:
`0x22b7a0` = **2,275,232 bytes**; the 4-MiB app partition has **46% free**.
SHA-256: `27fc28094ba8aeea05f1a0edefdfcb6dcf2db97ff7835751d81ec618077bb5a0`.
Log: `.local/spectrum3d/build-verified.log`. The initial full compile and two
incremental verifications are preserved separately; the latter include allocation
failure handling and the frozen-FPS telemetry correction found during review.
No flash command was executed. This is a development app build, not a release bundle.

Warnings observed in the full compile included ESP-IDF's literal-suffix warning
in `esp_private/periph_ctrl.h`, volatile increment warnings in dependency code,
and the bootloader's small remaining margin: 0x220 bytes (2%). These are distinct
from the app partition's available space. No dependency update was performed.

## 13. Performance evidence available without hardware

Tests establish bounded memory, no per-frame heap allocation, peak retention,
time-correct mapping, bounded pixel writes, and the buffer-retirement state model.
The firmware build validates the actual SDK/M5GFX APIs and integration.
These facts establish a code path capable of **targeting** 60 Hz, not its achieved
rate, visual tearing behavior, panel timing, PSRAM bandwidth, or RF stability.

`RTL_VIS PERF` reports submission FPS, source FPS, history insertion Hz, refresh
event Hz, render count, missed deadlines, history drops, submission/exit errors,
actual bucket count/raster size, visible span count, and quality.
`RTL_VIS PERF TIMING` reports frame mean/p95/p99 over the latest 128 frames,
plus per-frame history, combined geometry/raster, submission and ownership-wait
means since the last reset; producer reduction/lock time is reported in µs/second.
`RTL_VIS PERF RESET` starts a measurement window. It requires normal authorization.
The two query commands are read-only. Submission FPS is not physical scanout proof.
There is no periodic serial spam. CPU utilization and thermals are not instrumented.

## 14. Tab5 hardware validation plan

Only after separate flash/device authorization:

1. Preserve the recovery image and current logs, use the approved flash procedure,
   then monitor the agreed port with `idf.py -p COM17 monitor --no-reset`. Do not
   compete with an existing monitor. Record firmware hash and hardware revision.
2. Establish a 60-second RF/audio baseline on the same band/sample rate before
   opening Spectrum3D. Capture `RTL_HEALTH` and source/USB/audio drop counters.
3. Open Spectrum3D in the UI. Existing saved preferences are respected, so select
   Surface explicitly. Via the authenticated console, set:

   ```text
   RTL_VIS SET spectrum3d.mesh_mode 1
   RTL_VIS SET spectrum3d.color_mode 2
   RTL_VIS SET spectrum3d.history_s 20
   RTL_VIS SET spectrum3d.slices 4
   RTL_VIS SET visual.quality 0
   RTL_VIS PERF RESET
   ```

4. Warm up for 30 seconds, reset again, then collect `RTL_VIS PERF`,
   `RTL_VIS PERF TIMING`, `RTL_VIS STATUS`, and `RTL_HEALTH` once per second for
   at least five minutes. Preserve raw replies, not just averages. Repeat with
   Balanced and Radio Priority, then 128 slices and 60-second history.
5. Target criteria: sustained submission rate at least 59 Hz with panel refresh
   near 60 Hz, mean frame work below 16.67 ms, p95 below 16.67 ms and p99 below
   20 ms, missed deadlines below 1%, zero submission/exit errors, and no material
   RF/audio drop increase versus baseline. These are proposed acceptance criteria,
   not results. Use high-frame-rate video of identifiable moving traces to check
   unique presented frames and tearing; counters alone do not establish this.
6. Isolate bottlenecks using history, geometry/raster, submission and wait timings.
   Geometry and raster share one counter; only if dominant, add temporary finer
   profiling. The normal render budget is 16.67 ms including miscellaneous UI work;
   no measured subdivision is currently available.
7. Exercise 2/5/20/60-second windows, uneven/stalled input, narrow carriers,
   history filling and wrapping, freeze/unfreeze, all camera/render controls,
   source loss/recovery, drawer/chooser overlays, and repeated entry/exit.
   Require constant time-based motion, visible carriers, orderly fading, no
   foreground bleed-through, and no stale UI or unsafe exit.
8. Run a 30-minute soak: sample free/minimum heap from `RTL_HEALTH`, require no
   post-warmup downward trend or watchdog/underrun, and verify usable audio/touch.
   Record `main_stack_hwm` and DMA free/largest-block values from `RTL_HEALTH`.
   Measure task runtime only if available in the authorized diagnostic build;
   otherwise record CPU utilization as unavailable.
   Record ambient/case temperature with an external probe if available, rather
   than inferring temperature from FPS. Compare repeat runs at the same load.

## 15. Remaining bottlenecks

- CRITICAL acceptance gate: physical scanout/retirement, sustained FPS and RF
  coexistence remain untested on a Tab5. This is not an identified source failure.
- HIGH potential cost: native-resolution M5GFX span calls and PSRAM framebuffer
  bandwidth/cache writeback. Measure before choosing a lower-resolution mode.
- MEDIUM: active overlays force chrome refresh; triple buffering adds memory and
  retirement latency; 256 buckets merge nearby carriers; 8-bit history quantizes levels.
- LOW: a coherent snapshot copy, bounded projection/color work, and serial query cost.

## 16. Before/after comparison

| Area | Before | After |
| --- | --- | --- |
| FPS cap | 15/10/8 Hz and FFT revision gated | Independent 60 Hz target; hardware result unmeasured |
| Buffer wait | Fixed 20 ms | Spectrum3D refresh-event retirement, three buffers |
| History timing | Row count/fill dependent | Actual elapsed time |
| History Time control | Unused | 2–60 seconds; reconfiguration resets time buckets |
| Spectrum sampling | Skipped bins | Max reduction over every source bin |
| Narrow peak preservation | Carrier could vanish | Peak retained within declared bucket resolution |
| Surface fill | Lines only | Blue filled spans plus cyan ridges |
| Occlusion | Older lines could overwrite foreground | Front-to-back per-column horizon |
| Camera controls | Hardcoded geometry | All Spectrum3D camera/render controls connected |
| Age fade | Row-index height reduction | Time-based dimming and smooth terminal rolloff |
| Memory behavior | Up to 4 MiB shared history, two framebuffers | ~65.5 KiB terrain history/snapshot, three framebuffers |
| Performance telemetry | Basic source/presentation rates | Stage times, percentiles, rates, misses/drops, complexity |
| Automated tests | No focused terrain checks | Optimized and sanitizer checks plus synthetic preview |

## Authorized flash follow-up

App-only flash to COM17 (ESP32-P4 revision v1.3) succeeded at 0x10000.
The installed partition map matched the build. Esptool reported Hash of data verified.
Image SHA-256 remains 27fc28094ba8aeea05f1a0edefdfcb6dcf2db97ff7835751d81ec618077bb5a0.
NVS, PHY, bootloader, partition table and coredump regions were not written.
The initial no-reset connection failed; the normal reset retry succeeded.
Log: .local/spectrum3d/flash-retry.log. Runtime FPS and physical acceptance remain separate gates.
