# Local Codex handoff: install OrcUI Desktop Simulator on Windows

You are Codex running **on the user's Windows development PC**, not a cloud container. Work in `hardcoreerik/OrcSDR`. Goal: install and validate the **native LVGL 9.3 / SDL2 OrcUI desktop simulator** created in `codex/orcui-adaptive-display-poc`.

## Guardrails

- Do not flash either Tab5 or Waveshare as part of this desktop simulator task.
- Do not change ESP-IDF, M5GFX, `esp-rtl-sdr`, radio decoders, or working production firmware.
- Do not discard dirty worktrees or overwrite local changes. Use a dedicated worktree if necessary.
- Real LVGL GUI required: do not substitute a static screenshot, HTML mockup or unrelated simulator.
- Network and software installation are permitted for the PC simulator only. Install via MSYS2 UCRT64, as scripted.
- Keep a log beside the scripts in `experiments/orcui/desktop`.
- Report actual commands, exit status, screenshots, and logs. Do not claim install or hardware validation without evidence.

## Steps

1. Inspect `git status` and active branch. Fetch `codex/orcui-adaptive-display-poc`. Use a new worktree for this branch if a clean checkout is not available.
2. Review `experiments/orcui/desktop/README.md`, `CMakeLists.txt`, `lv_conf.h`, `main.cpp`, and `scripts/install-windows.ps1`.
3. Run from PowerShell in the checked-out branch:

   ```powershell
   cd experiments/orcui/desktop
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-windows.ps1
   ```

4. If installation or compilation fails, diagnose the **first concrete** failure, patch OrcUI files only, retry and commit fixes to a dedicated Codex branch or PR. Confirm LVGL v9.3.0, SDL2 and CMake imported targets and API compatibility. Fix Windows/MSYS2 quoting and compiler warnings as necessary.
5. Run `ctest --test-dir build --output-on-failure` in the MSYS2 UCRT64 environment. Verify headless SDL smoke passes.
6. Launch with `scripts/run-windows.ps1 -Preset waveshare`. Confirm moving synthetic spectrum and waterfall, MENU, SPAN, MODE/FILTER/GAIN button events, pause/resume. Use the SIM dropdown for Tab5, round480, round360, ultrawide, portrait and compact.
7. Run automated 2-second smoke cycles with `orcui_desktop.exe --preset waveshare --seconds 2 --cycle-ms 600` (under `SDL_VIDEODRIVER=dummy`) and interactive 10-minute GUI soak. Attach a screenshot from 720x720 and one from 1280x720 to the handoff.
8. Finally, run the pure host C++ tests in `experiments/orcui/tests` (they must pass in Release, not only Debug). Fix any failures, commit and push to a research branch, and provide a PR for review.

## Report required

- PC / Windows version, compiler, CMake, LVGL/SDL2 versions
- Exact install/build/test commands and exit codes
- Files changed and commit/PR URL
- Screenshot from two screen profiles
- `install-windows.log` and `orcui_desktop.log` location
- What is verified, what is still unverified (including physical P4 and real RTL-SDR integration)

Stop and ask user if installation requires deleting unrelated software, replacing globally installed SDKs, or modifying working firmware.
