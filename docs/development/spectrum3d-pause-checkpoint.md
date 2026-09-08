# Spectrum3D pause checkpoint - 2026-09-08

Paused at the user's request. Branch: `codex/spectrum3d-history-60fps-recovery`, based on `376230ab9aa49c7abaf36c6c42e7b5a1dde41140`. This is not a 60 FPS release or merge candidate.

## Completed work

Original renderer patches survived the rebase unchanged. Exact source is recovered, but the identity of the earlier physically observed fast firmware is unknown. Recovery restores filled blue/cyan terrain without sparse row reduction, fixes drawing-coordinate handling, moves timing storage to PSRAM and adds per-stage timings and build manifests. Fresh A/B/C/D native builds, optimized/sanitizer history/adapter tests and focused radio-scan tests passed. The forensic report records exact commands and comparisons.

## Resume here

- Optimize measured drawing (~46.5 ms), geometry (~27 ms) and clear/grid (~22.8 ms), preserving terrain detail. Submission (~0.5 ms) and buffer waiting (~0 ms) do not support a fixed presentation-cap explanation.
- Review M5GFX framebuffer rotation/layout and drawing paths before choosing batching, direct raster writes or DMA. Verify cache/scanout ownership. Do not change RF priorities based on the earlier unverified starvation explanation.
- Keep this branch separate from the original experimental worktree. Retain current radio dependency pins; do not restore the sparse wire experiments.
- After changes, run focused tests and a native build, then obtain authorization for a new flash. Remaining gates: >=59 submissions/s, physical tearing/unique frames, source loss, audio playback, touch/overlay coverage and a 30-minute soak.

## Preserved state

Device left on Spectrum3D at 96.1 MHz, 960 kS/s, Balanced quality, 128 requested rows, 20-second history and Surface mode. User reported "this runs good" and cycled modes. Prior controls remain in local `device-d-saved-controls.json`; the tested controls were left active. No monitor or capture process is intentionally left running.

Local evidence: `F:/Ai/OrcSDR-Temp/spectrum3d-recovery-evidence-20260908` contains raw measurements, logs, manifests, saved controls and historical build copies. Large binaries/raw logs remain local; the hardware summary below is committed for remote continuity.

The flashed image predates this checkpoint commit. Identify it using the source digest/BIN hash below, not the later checkpoint SHA. Rebuilds after committing may embed a different identity.

# Recovery D hardware test — September 8, 2026

User separately authorized flashing and testing. Application only flashed at 0x10000 on COM17; esptool verified the data hash. C6 and NVS were preserved.

Running identity: terrain-recovery-1, base SHA 376230ab9aa49c7abaf36c6c42e7b5a1dde41140, dirty=1, digest 235c20af577866a9852e49463117e86091ac4e64b12d48f3e71a372cc40da911. BIN SHA256 81ce591a5fcdd1c8c5980bcdd6a56f19cfdd1b1741db8fab6e3240341b599bde.

Boot reached Ready, then streamed 960000 samples/s at 96100000 Hz. Initial ready DMA largest block was 32768 bytes. UI CHECK passed at Home before the test and after exiting Spectrum3D. Freeze ON/OFF and close/reopen commands succeeded. User reported "this runs good" and rapidly cycled visualizer modes, returning to Spectrum3D. This is favorable functional feedback, not an explicit tearing/audio certification.

The first five-minute capture includes user mode cycling and is retained as transition/stability evidence. A second five-minute capture was started with Spectrum3D untouched. Source-loss and a 30-minute soak were not performed. No physical frame-count video was collected.

Files: flash-d.log, device-d-measurements.jsonl (first run), device-d-stable.jsonl (untouched run), device-d-saved-controls.json (prior controls).

Monitoring note: the first native monitor invocation omitted -B and began configuring an unused default build directory; it was stopped. The corrected invocation used build-native-hosted3 and --no-reset. No build or second flash occurred. Its process tree was closed before the serial measurement client opened COM17. The flashed BIN remains the verified artifact.

## Completed stable run

```json
{
  "samples": 300,
  "all_spectrum3d": true,
  "all_source_present": true,
  "free_heap_min": 24826028.0,
  "free_heap_max": 24826160.0,
  "rf_sps_min": 959959.0,
  "rf_sps_max": 959980.0,
  "driver_drops_max": 0.0,
  "audio_chunks_max": 0.0,
  "last_perf": "RTL_VIS_PERF submit_fps=9.8 source_fps=25.5 history_hz=5.46 refresh_hz=66.1 frames=2927 missed=15081 history_drops=8 submit_errors=0 buckets=128 raster=598x295 spans=25239 quality=1 visible_rows=110 submissions=2927 target_hz=60 completion_identity=unavailable",
  "last_timing": "RTL_VIS_TIMING unit=us frame_avg=97952 p95=99568 p99=100340 history_avg=538 geometry_raster_avg=26979 clear_grid_avg=22849 draw_avg=46534 overlay_avg=306 submit_avg=519 wait_avg=0 producer_us_per_s=12816 samples=128"
}
```

Result: boot and basic UI checks PASS; sustained 60 FPS target FAIL. 2927 submissions in approximately 300 seconds (~9.75/s), 15081 missed deadlines (~83.7% of submitted+missed opportunities), eight history lock drops, zero submission errors. Final p95/p99 reflect the last 128 frames, not whole-run quantiles. Drawing, geometry and clear/grid dominate; submission and buffer waiting do not. RF delivery stayed near 960 kS/s with no driver drops. Audio playback not validated: audio_chunks=0. No reset observed during the stable run. Device left on Spectrum3D with the recorded test controls. Next work should optimize measured rendering costs while retaining geometry and verify again; no new code, commit or PR was made in this hardware-test turn.
