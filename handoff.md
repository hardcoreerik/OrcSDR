# OrcUI — Local Codex Handoff

**Handoff ID:** ORCUI-LOCAL-001
**State:** READY
**Owner:** Local Codex (tester / hardware integrator)
**Repository:** `hardcoreerik/OrcSDR`
**Development branch:** `codex/orcui-adaptive-display-poc`
**Report mailbox:** https://github.com/hardcoreerik/OrcSDR/issues/195

> This file is an explicit work order. A new branch commit does **not** automatically mean a new work order. Act only when **Handoff ID** changes or this handoff explicitly requests repeating work for a newer SHA. Record the processed ID and SHA to avoid loops.

## Before taking action

1. Read `docs/orcui/LOCAL_VALIDATION_LOOP.md`, `docs/orcui/RESEARCH.md`, `docs/TAB5_BUILD_POLICY.md`, and issue #195. Their safety and reporting rules apply.
2. Record the exact remote branch SHA and local working-tree status. Fetch into a separate, clean test worktree. Do not reset, modify, rebase, merge, or force-push the developer branch.
3. Save all command output and observations to a dated log **in the directory the test runner is invoked from**. Redact credentials and local network details.
4. Do not infer hardware success from simulator output. No destructive flash, NVS erase, partition changes, or automatic device selection. Obtain an explicitly confirmed target, port, and compatible image before any flash.

## Assignment ORCUI-LOCAL-001: host gates and hardware readiness

**Goal:** Produce reproducible evidence and identify integration blockers, without destabilizing the shipping Tab5 application.

1. **Inspect what exists on the development branch.** Inventory `experiments/orcui/`, the host test/build entry points, and any P4/BSP targets. The current baseline is an experimental layout/RGB565 raster implementation, *not* a flashed LVGL application.
2. **Run the available host tests**, or identify accurately which tests/build instructions are missing. Validate rectangle 1280×720, square 720×720, circle 480×480 and 360×360, ultrawide 2560×720, plus portrait 480×800. Verify clipping, positive-sized rectangles, non-overlapping essential controls, and touch hit rectangles. If a test harness is absent, propose or implement it on a **separate** `local/orcui-tests-*` branch and submit a PR **targeting the developer branch**; never overwrite it.
3. **Inventory local P4 test readiness:** which exact Tab5 or Waveshare P4 board is physically available, screen/panel and touch controller, toolchain and ESP-IDF versions, serial port, free space, and known-good current firmware. Do not claim a board is connected unless independently verified.
4. **Review vendor BSP compatibility** for the *specific* target, especially LVGL 9, IDF 5.5.4, MIPI-DSI framebuffer ownership, DMA/cache synchronization, and touch calibration. Identify *versioned* blockers and proposed isolated workarounds. Do not automatically change pinned firmware dependencies.
5. **Report** one meaningful HOST ONLY, BUILD ONLY, or BLOCKED result to issue #195, using its required template, with the exact SHA, commands, tool versions, objective measurements, and next-step recommendation. Log locally in the invocation directory.

## Stop condition / next pickup

- Once ORCUI-LOCAL-001 has been reported, mark this handoff ID locally as processed; wait for a **new Handoff ID**. Avoid reexecuting unchanged instructions because a documentation commit landed.
- You may monitor at 15-minute intervals **only if an actual local scheduler or persistent process exists**. The repository's `tools/orcui-watch.ps1 -IntervalSeconds 900` is a read-only SHA watcher; it **does not** autonomously execute Codex.
- Never perform simultaneous changes on `codex/orcui-adaptive-display-poc`. Place proposed fixes in separate local branches/PRs.
- If the user tells you to stop, stop promptly; do not schedule more work.

## Developer-side next step (information, not a local instruction)

The cloud development side is building the portable host tests, the LVGL 9 integration proof, and the RF graphics benchmark. Local Codex should consume those deliverables as they appear and provide physical evidence, not independently rewrite the production dashboards.
