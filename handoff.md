# OrcUI — Local Codex Handoff

**Handoff ID:** ORCUI-LOCAL-002
**State:** READY — supersedes ORCUI-LOCAL-001; carry forward any unfinished host/board inventory gates
**Owner:** Local Codex (integration, Waveshare physical testing, evidence)
**Product:** hardcoreerik/OrcSDR
**Developer branch:** `codex/orcui-adaptive-display-poc`
**Source hardware branch:** `codex/waveshare-4b-fm-dashboard`
**Report channel:** https://github.com/hardcoreerik/OrcSDR/issues/195
**Architecture contract:** `docs/orcui/INTEGRATION_MILESTONE_002.md`

> Do not use commits alone as new assignments. Claim the *Handoff ID*, capture the current SHA(s), execute the work exactly once per ID, then report and wait for ORCUI-LOCAL-003. The user's local Luna 15-minute watch is already running; do not create a second timer or duplicate run. If the user says stop, stop.

## Mission — build and physically validate the first genuinely adaptive OrcSDR Home on Waveshare 4B

This milestone must **replace the new prototype's 720-only dashboard layout with the shared OrcUI layout and Home view**, NOT invent another set of screen-specific UI files. Keep an unchanged, selectable legacy Waveshare Home v3 path as a performance and visual baseline. The native Tab5 production UI, USB receiver driver, and C6 are not to be altered.

## First: read and checkpoint

1. Read this handoff, `docs/orcui/INTEGRATION_MILESTONE_002.md`, `docs/orcui/LOCAL_VALIDATION_LOOP.md`, `docs/orcui/RESEARCH.md`, `docs/TAB5_BUILD_POLICY.md`, `docs/hardware/WAVESHARE_4B_USB_HOST_POWER.md`, `docs/hardware/WAVESHARE_4B_FM_DASHBOARD.md` (from its source branch), and issue #195.
2. Record `git status --short`, current HEAD, and the full SHA of *both* remote branches after `git fetch origin`. Preserve all uncommitted edits and local sessions. The SHA references in these instructions are provenance examples, **not** a reason to reset a newer branch.
3. Create a separate clean worktree / `local/orcui-4b-integration-002` branch **based on the latest OrcUI development branch**, without modifying either watched branch or `main`. Bring in the **exact** `apps/orcsdr-waveshare-4b/` tree from the *recorded SHA* on `codex/waveshare-4b-fm-dashboard` (e.g. via `git restore --source=<recorded-waveshare-SHA> -- apps/orcsdr-waveshare-4b`). Record imported provenance and do not silently blend concurrent changes.
4. Maintain one timestamped log **in the directory where each script/test is run**. Also capture SHA, IDF/IDF Component Manager versions, Python/compiler version, compile commands, full test outputs, flash arguments, boot logs and UI metrics. Scrub secrets and LAN identifiers.

## Build the cross-board UI adapter — implementation requirements

1. Treat `experiments/orcui/include/orcui/layout.hpp` + `layout.cpp` as the **one canonical adaptive Home geometry engine**. `experiments/orcui/home_lvgl.cpp` is the **one new shared Home LVGL implementation**. Do **not** use `home_v3_drawer_layout.hpp`, `fm_4b_layout.hpp`, or `components/orcsdr_ui_layout` to place widgets in the adaptive Home. Legacy references may remain compiled only in the explicitly selected old UI.
2. Link OrcUI as an opt-in ESP-IDF component from the standalone Waveshare app (e.g. via an explicit `EXTRA_COMPONENT_DIRS` declaration and IDF dependencies). Pin existing: ESP-IDF 5.5.4, Waveshare BSP 3.0.1, LVGL 9.3.0, esp_lvgl_adapter 0.6.3, esp-rtl-sdr commit `31a159df4f006a837d5041029bc6d1bc63520234`. **Do not bump these simply to resolve compile errors.**
3. Implement a narrow Waveshare-to-OrcUI adapter, e.g. `apps/orcsdr-waveshare-4b/main/orcui_4b_adapter.{hpp,cpp}`. Acquire true panel width/height from the initialized LVGL/BSP display, not a hard-coded 720, and create `orcui::HomeLvgl` with its `Display` profile. No board-specific dimensions or `M5.Display` calls are permitted inside the shared Home view.
4. Map the EXISTING legitimate IQ-only Home state (requested/applied Hz, effective SPS, drops, uptime, receiver state) into truthful `HomeData`. Do not invent WLAN, audio, gain or RDS data. Adapt `HomeCommand` to the existing radio task's bounded command queue so **tune down/up, step, palette and drawer actions** actually work; enable an FM IQ-preview route only if the already built five-tab FM screen is safely integrated. Keep unsupported actions visibly unavailable, not inert lookalike buttons.
5. Preserve real OrcSDR badge and source palettes/green/cyan theme. Badge opens/closes LAST USED drawer; tapping the overlay scrim closes it **without tuning-click-through**; plotting continues with the drawer open. Avoid plot buffer reallocations on drawer toggle. Correct focus/touch hitboxes as needed.
6. Use `orcui::paint_spectrum` and `orcui::push_waterfall` with width/height/stride-driven RGB565 buffers. Explicitly allocate sizable buffers in PSRAM, verify alignment/stride and LVGL canvas lifetime; prevent concurrent SDR/UI modification or asynchronous flush use-after-free. ESP-IDF BSP/LVGL is the sole framebuffer owner; no parallel M5GFX drawing. ISR/IQ/USB processing must never call LVGL or wait for drawing.
7. Provide a compile-time or runtime selection of **LEGACY_HOME_V3** vs **ADAPTIVE_ORCUI_HOME** for A/B comparison, with legacy as safe fallback initially. Do not copy Home widgets into a third 720-only implementation just to make it look right.
8. Improve shared `HomeLvgl` where necessary (e.g. pointer/label lifetime, supported commands, modal scrim, robust canvas allocation) *in your isolated PR*, not by pushing to the developer branch.

## Test gates — run in this order

**A. Host, no flashing.** Run the OrcUI CMake/CTest host suite (`cmake -S experiments/orcui/tests -B <host-build-dir>`, `cmake --build <host-build-dir>`, `ctest --test-dir <host-build-dir> --output-on-failure`). If any host target is missing, implement it in the isolated branch. Cover 1280x720 rectangle, 720x720 square, 480/360 circle, 480x800 portrait, 2560x720 ultrawide; assert all required controls reachable, drawer backdrop prevents click-through, no overlapping/cropped interactive controls, and unchanged radio action semantics. These are layout/simulator results, **not physical support claims**.

**B. Native P4 build.** Use a fresh independent Waveshare build directory with silicon v1.x overlay (not the rev3.x factory defaults). Example from `apps/orcsdr-waveshare-4b` in an exported native ESP-IDF 5.5.4 environment:

```powershell
idf.py -B build-orcui-4b -D IDF_TARGET=esp32p4 -D SDKCONFIG=build-orcui-4b/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.rev1_x" build
```

Check generated sdkconfig, linker output, app partition capacity, flash mapping, and complete firmware hashes. The physical P4 was historically v1.3 with 32MB flash, 32MB PSRAM at 200 MHz; **re-probe**. If the binary does not fit the existing safe partition, **do not change the partition/NVS layout under this handoff**: report the blocker and possible non-destructive alternatives. Preserve a known-good legacy build for rollback.

**C. Waveshare hardware — FLASH AUTHORIZED.** The user explicitly authorized firmware flashing to their **Waveshare ESP32-P4-WIFI6-Touch-LCD-4B** for this project. You **do not need to ask for repetitive flash approval**, provided the target is positively identified, its serial port and silicon revision are confirmed, image/partition compatibility is checked, and the operation is non-destructive. The previous COM40 assignment is *historical only*, not guaranteed; COM39 previously served another board. This permission does not include Tab5 flashing, C6 flashing, chip erase, NVS erase, unsafe power wiring or a partition-table migration. If identification/compatibility fails, stop and report BLOCKED.

**D. Physical UI and RF evidence.** Capture startup from reset, display/touch controller, screenshot or owner photo if available, badge/drawer repeated toggles, outside dismissal, 50 touches across enabled controls, frequency+step log equivalence, palette, real vs unavailable status, return from FM IQ preview (if enabled), no crash, memory health and benchmark against LEGACY_HOME_V3. For live RF, use only the previously verified electrically safe externally powered HS SDR fixture; never bridge USB-C 5V/CC or backfeed the board. Capture `HOME_4B RF START` followed by >=300 seconds, target >=95% of configured 960kS/s, report USB drops/overruns and available p50/p95 rendering times, FPS, DMA heap minimum, PSRAM usage. Run `tools/hardware/check_4b_home_log.py` if compatible; record any parser adjustments and do not silently relax qualification.

## Deliverables and stop criteria

- An isolated `local/orcui-4b-integration-002` implementation branch and a draft PR **targeting `codex/orcui-adaptive-display-poc`**, with the exact imported Waveshare SHA and reason for each added source. No merge to any protected/developer/hardware-source branch.
- All new adaptive Home widgets must come from **one** `orcui::HomeLvgl` and `orcui::plan()`, not a third 720-specific layout. Include host tests, native-build output, baseline-vs-adaptive comparison and a short implementation README.
- One meaningful report to issue #195 containing the handoff ID, BOTH branch SHAs, exact build/flash commands and port, observations, artifact/log locations, objective performance counters and clear **HOST PASS / NATIVE PASS / DEVICE PASS / RF PASS / FAIL / NOT TESTED / BLOCKED** statuses. Never claim RDS, FM audio, Tab5 LVGL parity, or round physical-display success from this test.
- If the full integration cannot safely complete in one run, leave a draft PR/work branch with honest blockers, preserve the known-good firmware, report interim evidence, and await a specific subsequent handoff. Do not repeatedly flash or re-run on every scheduled wake.
- Mark ORCUI-LOCAL-002 as processed after reporting, then await a new Handoff ID. No new scheduler is needed; Luna is already watching.

**Priority order:** source/data correctness > device safety > no regression in SDR > functional adaptive UI > pixel-perfect styling.
