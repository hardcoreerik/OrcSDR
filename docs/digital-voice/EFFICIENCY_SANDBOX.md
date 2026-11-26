# DV efficiency — isolated baseline and acceptance contract

Frozen public base: bd7cb282bf93cf72e4d65859959d50c674632161, parent 62558be1f53e4fb9df91ff0364b3734b2ffa16d0. Owner's unpushed local changes are not available here. No main merge/PR or device access. Root AGENTS.md absent in pinned tree; UI AGENTS and rendering standard read. No UI changes.

## Owner supplied physical baseline (not remeasured)
2026-11-27 label in owner report: dv_rx core0 priority2 71–82%, main 3%, ~850 ms stalls, core1 ~80% idle, ~2 ring drops/s, DMR wall 4.0 vs 0.9 us/pair as reported; dec_internal 24144 B, ring 262144 B, internal free 15–22 KiB, largest 16 KiB. Voice synth 55442/133191/91714 us avg/max/last per group, pump maximum 83 ms. Actual denominators/CPU vs wall require confirmation on device. Existing source already decimates NXDN48 2x, respects protocol mask, and has dv_voice pinned core1 priority4 plus recursive oscillators in mbelib. Do not duplicate these. Existing worker_busy_pm and stage timings are wall intervals, not task CPU cycles. The earlier BENCHMARKS.md is historical and not interchangeable with this base.

## Host profiling
`python tools/dv_efficiency/profile_host.py --root /path/to/clean/checkout --output evidence/base`
Use a disposable checkout (temporarily instruments two host test files then restores). For candidate add `--compare evidence/base/report.json`. Builds Release native live/corpus/voice and dashboard suites; three CPU/wall runs per real input and synthetic noise for masks 1,2,4,7; host real stage wall timing from bench; exact hashes of every printed ordered event and native per-call wire/PCM and speaker WAV files. Hashes and counts only are persisted; raw audio/IQ and frame dumps stay temporary. Not a physical benchmark or full event-struct comparison. No data-dependent thresholds or audio output changes are allowed. Need actual ESP32-P4 CPU and memory, cold/warm runs, no hidden drops, stall_count 30s after 4 min, clean audio, and actual COM17 authorization before hardware acceptance.

## Coordination
Codex owns integration and device. Sandbox owns narrow decoder/vocoder improvements and independent A/B harness. No retune, driver, dashboard, file format, task core/priority, ring-size changes until hardware evidence. Do not shrink buffers to mask throughput failures. Keep each optimization independently reversible.
