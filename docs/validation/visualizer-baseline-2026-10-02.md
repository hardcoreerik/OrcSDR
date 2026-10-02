# RF Visualizer Baseline — 2026-10-02

Evidence capture for the [RF Visualizer resource-efficiency rebuild](../superpowers/specs/2026-10-01-rf-visualizer-resource-rebuild.md)
(Phase 0). This is a **pre-change** baseline: it records the as-shipped
behaviour that the rebuild phases will be measured against. Nothing in this
document is an improvement claim.

## Conditions

| Item | Value |
|---|---|
| Device | M5Stack Tab5, ESP32-P4 v1.3, MAC `30:ed:a0:e2:e6:95` |
| Firmware | `v0.3.0-beta.2` + channelizer single-channel fix (branch `rf-visualizer-resource-rebuild`) |
| Receiver | Nooelec NESDR SMArTee **V5** (RTL2838, USB serial `36309640`) |
| Antenna | MLA30+ |
| Signal | FM broadcast **96.1 MHz**, WBFM, stereo locked, RDS carrier present, ~-22.7 dBFS, 260 kHz filter |
| Display | rotation 1 (1280x720 logical on a native 720x1280 DPI panel) |
| Visualizer quality | Balanced (`quality=1`) |
| Settle | 30 s per view before sampling + capture |
| Capture tool | `tools/capture_visualizers.py --settle-seconds 30` |

`presentation_fps` and `analysis_fps` are the values reported by
`RTL_VIS STATUS`. `dropped` is `input_drops` from the same line (cumulative
IQ offers the analysis task could not accept).

## Per-view results

| View | presentation_fps | analysis_fps | dropped | Result |
|---|---|---|---|---|
| spectrum | 7.9 | 7.9 | 104 | captured |
| waterfall | 13.0 | 13.0 | 180 | captured |
| phosphor | — | — | — | **DEVICE RESET (watchdog)** — fixed, see below |
| spectrum3d | — | — | — | pending |
| constellation | — | — | — | pending |
| iqscope | — | — | — | pending |
| polar | — | — | — | pending |
| occupancy | — | — | — | pending |
| peakavg | — | — | — | pending |
| doppler | — | — | — | **DEVICE RESET (watchdog)** |
| channelizer | — | — | — | pending |
| audiospec | — | — | — | pending |

The table is the pre-change baseline and is intentionally left as recorded. The
phosphor fix and its post-change numbers are in
[Phosphor root cause and fix](#phosphor-root-cause-and-fix-server-side-measurement)
below; the other views are still unmeasured.

## Crash findings

Both resets were confirmed via `RTL_HEALTH STATUS` as
`reset_reason=7` = **`ESP_RST_WDT`** (watchdog reset); the device rebooted to
Home with the visualizer closed in both cases.

- **doppler** — reset observed while on the doppler view. After reboot the
  persisted view was `doppler`.
- **phosphor** — reset during the 30 s live settle; `RTL_VIS STATUS` returned
  `active=0 view=doppler source=1`.

These are the two views the rebuild model identifies as the heaviest
(~90 MB/frame of scattered PSRAM traffic from per-row pushes through the
rotated panel path), so the crash is consistent with Core-1 starvation
rather than an unrelated fault.

Later measurement refined this: moving the accumulation work off the analysis
task (so Core 1 became copy-only) did **not** stop the phosphor reset — it only
raised the frozen frame rate. The crash was draw-path driven, i.e. the plot
write itself starving the idle task, which is what the column-oriented rewrite
below addresses.

## Observations

- **Even the lightest views are slow with FM audio live.** spectrum runs at
  7.9 fps and waterfall at 13.0 fps.
- **`analysis_fps` equals `presentation_fps` on both views.** The `rf_analysis`
  task should produce ~30 frames/s at the Balanced 33 ms interval; it is
  producing only 8–13, i.e. it is starved on Core 1 while FM DSP/audio runs.
- `dropped` (104 then 180) confirms IQ offers are being discarded because
  analysis cannot keep up.
- Modes are not isolated from DSP/audio: several visual modes change the
  demodulated sound while active (operator-observed), so baseline audio
  quality is not constant across the sweep.

## Artifacts

PNGs are written to `artifacts/visualizer-captures/` (`visNN-<view>.png`),
with raw BMPs under `raw/`. Note the capture tool downloads only after the
full selected set is captured, so a crash during the sweep also loses that
run's earlier images — the sweep is therefore run in small batches.

## Phosphor root cause and fix (server-side measurement)

The phosphor reset was reproduced, then instrumented with per-frame
`micros()` counters printed over the debug serial port, which replaced
guesswork with a measured breakdown. The figures below are from the same
harness before and after the fix, so they are directly comparable.

| Stage | Before | After |
|---|---|---|
| density snapshot memcpy (303 KB) | 8 ms | 8 ms |
| scan (kPlotW x g_density_rows = 354k iterations) | **381 ms** | **22 ms** |
| emit (10,300 `fillRect` calls; later raw stores) | 37 ms | 24 ms |
| strip loop total (incl. plot background clear) | 449 ms | 48 ms |
| frames drawn per second | **1.8** | **8.0** |

Two hypotheses were tested and **rejected** before the real cause was found,
which is worth recording so they are not re-tried:

- The scan's `density_display_intensity()` float math (a libm `lroundf` per
  row) looked like the obvious cost. Hoisting it into a 256-entry LUT moved
  the strip loop only 449 -> 428 ms (~5%).
- A `pushImage` of a whole 1x590 column measured 40.6 us/call (68.8 ns/pixel),
  *worse* than the `fillRect` run loop it would have replaced.

The actual causes, in order of size:

1. **Strided scan.** The density map was row-major, so the renderer's scan down
   a screen column walked a `kBins`-byte stride: one cache-line fetch per
   single byte, 354k per frame (~22.7 MB of PSRAM traffic) at 387 cycles per
   iteration. Storing the map **bin-major** makes each column contiguous.
2. **Per-call overhead in the emit path.** `LGFXBase::fillRect` measured ~1.9 us
   of call overhead for only ~2-3 cache lines of payload. Under this panel's
   rotation M5GFX swaps w/h, so a *logical column* is one contiguous span of
   the framebuffer and can be written with raw stores instead.
3. **Framebuffer stores are expensive.** Raw 32-bit stores measured ~16 cycles
   each (~45 ns per 2-byte pixel) regardless of width, i.e. roughly 45 MB/s of
   CPU write throughput into the framebuffer. This is the floor for the plot
   write and the reason further gains need DMA2D/PPA blits from cached memory
   rather than tighter CPU loops.

Note the rotation caveat: the before/after figures above were both taken with
the display at rotation 3, which is the shipped default on this branch.

## Post-fix verification

- **Crash eliminated.** phosphor renders live and unfrozen for 60 s with
  `uptime_ms` monotonic and no reset (previously `ESP_RST_WDT` inside 10-20 s).
- **Stable frame rate.** 7.0-8.0 fps across the 60 s window, `quality=1`.
- **Output verified correct.** `UI_CAPTURE phosphor` at rotation 3 renders the
  expected FM spectrum with persistence trails, plot frame, `PERSIST` legend
  ramp, mode badge and title. This matters because the fix changed both the
  storage layout and the pixel-write path, so a visual check is the only real
  proof the transposed indexing and the direct framebuffer mapping are right.

## Remaining known costs (measured, not yet addressed)

- `draw_frame_chrome()` repaints the full chrome every frame for full-repaint
  views, including a full-screen `fillScreen`: ~200 ms per second of wall clock
  at 8 fps (~25 ms/frame) spent redrawing chrome that has not changed.
- The 303 KB density snapshot memcpy is ~8 ms/frame.
- ~25 ms/frame of the total is still unattributed (other `service_ui` work:
  handles, HUD, labels, `drawRect`).
- doppler has the same row-push structure and the same confirmed WDT reset, and
  has not been converted to the column-oriented path yet.
