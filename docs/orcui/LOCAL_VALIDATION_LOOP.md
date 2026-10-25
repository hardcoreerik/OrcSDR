# OrcUI Cloud Development ↔ Local Hardware Validation

Status: **research/POC**. This document describes a repeatable test workflow, NOT proof that the adaptive UI already runs on real ESP32-P4 hardware.

## Source of truth

- Repository: https://github.com/hardcoreerik/OrcSDR
- Developer-owned branch: `codex/orcui-adaptive-display-poc`
- Hardware feedback mailbox: https://github.com/hardcoreerik/OrcSDR/issues/195
- Production: `main` (must remain untouched by experiments)

The cloud developer creates application and test code on the developer branch. The local Codex session is a **tester and reporter**. It must never push, reset, rebase, merge, or force-push to the developer branch. If a local source patch is required, create a separate branch and PR with base `codex/orcui-adaptive-display-poc`.

## Local test loop

1. Record `git status --short` and `git rev-parse HEAD`; never discard uncommitted changes.
2. Fetch with `git fetch origin codex/orcui-adaptive-display-poc`; use a **separate clean worktree**, detached at the fetched commit, for hardware tests.
3. Read `docs/orcui/`, the branch's relevant tests, `docs/TAB5_BUILD_POLICY.md`, and the changed-file diff against the previously tested commit.
4. If only documentation changed, record it; **do not flash**. If a host simulator or layout tests were added, run them first.
5. Build the exact target with a suitable version-pinned toolchain. Tab5's native ESP-IDF entry point is `apps/orcsdr-tab5/tools/build-tab5-idf.ps1`. Waveshare requires a compatible independent BSP target; never assume a Tab5 image can boot on Waveshare.
6. Before flashing, confirm the connected target, serial port, firmware build and compatibility of partition/NVS preservation. Never mass-erase NVS or flash the wrong board. If target verification is impossible, report **BLOCKED**.
7. Capture serial logs, host test output, exact build command, SHA, and compiler/library versions. Keep **a log in the local invocation directory** (e.g. `orcui-test-20261014-230000.log`). Mask passwords, tokens, and personal network data.
8. Post one issue #195 comment per meaningful test or blocker using the mandatory report template in the issue. Include failures with minimal reproduction steps. Do not claim hardware PASS from host-only execution or image renderings.
9. If the SHA is unchanged, don't retest unless the test environment or accepted procedure has materially changed. Recheck branch periodically; begin the next iteration when a new SHA appears.

## Required validation

### Host

- 1280x720 wide rectangle (Tab5)
- 720x720 square (Waveshare)
- 480x480 circular usable area (geometrically clipped)
- 2560x720 ultrawide (layout test only, NOT an assertion of panel electrical support)
- Header and menu accessibility, valid touch rectangles, readable labels, no offscreen required actions
- One shared state/action model, not separately rewritten dashboard functionality per device

### Hardware

- Record exact board revision, display controller, touch controller, ESP-IDF, LVGL, display BSP, and M5GFX versions
- Display init, touch mapping, touch calibration, safe area, spectrum/waterfall, menu modal and font legibility
- RF workload when available: USB streaming drops/overruns, audio underruns, p50/p95 render latency and refresh rate, minimum free internal DMA heap, PSRAM and memory-growth observations
- Compare Tab5 native production M5GFX baseline with prototype on comparable RF workloads
- Do not claim a pass for unmeasured items. Annotate each as PASS / FAIL / NOT TESTED / BLOCKED.

## Engineering constraints

- Remain native ESP-IDF; do not replace with Arduino or PlatformIO.
- Preserve driver pin and existing product firmware policies.
- No direct M5GFX drawing in RF IQ callbacks or decoder hot paths.
- Single framebuffer owner. No simultaneous M5GFX and LVGL writers to a DSI framebuffer.
- UI snapshots and events must not block USB receiver tasks.
- Prefer a shared LVGL 9 view model and layout rules, specialized RF pixel-buffer widgets, and display-specific `esp_lcd`/BSP adapters.
- Don't upgrade ESP-IDF, ESP-Hosted, M5GFX or RTL-SDR pins just to pass tests without a separate documented compatibility decision.
- Never claim a screenshot or software simulation verifies hardware.
