# VEIL-26 runes validation — 2026-10-29

Scope: Elder Futhark display only, branch `claude/dead-air-veil26`, based on
`c704495b7e3c8c9dc353e50594c059d2b99d13fe`. No push, merge, flash or device access.
Canonical engine and golden-test files are unchanged (`git diff --quiet` returned 0).

## Host commands and actual results

From PowerShell, `wsl bash /mnt/c/Users/hardc/AppData/Local/Temp/codex-veil26-runes/host.sh`.
The script executed the following exact commands, drawn from the CI workflow:

```bash
#!/bin/bash
set -eu
cd /mnt/f/Ai/OrcSDR/.claude/worktrees/dead-air-dashboard-review-8c2b12
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  -Itests/acars_ui_stubs -Iapps/orcsdr-tab5/ui \
  tests/dead_air_render_tests.cpp \
  tests/dead_air_ui_stubs/audio_header_stub.cpp \
  apps/orcsdr-tab5/ui/dead_air_dashboard.cpp \
  apps/orcsdr-tab5/ui/dead_air_runes.cpp \
  apps/orcsdr-tab5/ui/dead_air_sweep.cpp \
  apps/orcsdr-tab5/ui/dead_air_palette.cpp \
  apps/orcsdr-tab5/ui/dead_air_format.cpp \
  apps/orcsdr-tab5/ui/dead_air_info.cpp \
  apps/orcsdr-tab5/ui/acars_dirty_rects.cpp \
  apps/orcsdr-tab5/ui/dead_air_model.cpp \
  apps/orcsdr-tab5/ui/dead_air_bandplan.cpp \
  apps/orcsdr-tab5/ui/dead_air_fx.cpp \
  apps/orcsdr-tab5/ui/screen_controller.cpp \
  -o /tmp/dead-air-render-tests
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  -Iapps/orcsdr-tab5/ui \
  tests/dead_air_veil26_tests.cpp \
  apps/orcsdr-tab5/ui/dead_air_veil26.cpp \
  -o /tmp/dead-air-veil26-tests
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  -Iapps/orcsdr-tab5/ui \
  tests/dead_air_veil26_log_tests.cpp \
  apps/orcsdr-tab5/ui/dead_air_veil26_log.cpp \
  apps/orcsdr-tab5/ui/dead_air_runes.cpp \
  apps/orcsdr-tab5/ui/dead_air_veil26.cpp \
  -o /tmp/dead-air-veil26-log-tests
python3 -I tests/dead_air_runes_vectors.py
g++ -std=c++17 -O2 -Wall -Wextra -Werror \
  -Iapps/orcsdr-tab5/ui tests/dead_air_runes_tests.cpp \
  apps/orcsdr-tab5/ui/dead_air_runes.cpp \
  apps/orcsdr-tab5/ui/dead_air_veil26.cpp \
  apps/orcsdr-tab5/ui/dead_air_veil26_log.cpp -o /tmp/dead-air-runes-tests
/tmp/dead-air-runes-tests
/tmp/dead-air-veil26-tests
/tmp/dead-air-veil26-log-tests
/tmp/dead-air-render-tests

```

Final output (exit 0):

```text
Dead Air rune independent vectors PASS
Dead Air runes host tests PASS
dead_air_veil26_tests: PASS
dead_air_veil26_log_tests: PASS
Dead Air renderer host tests PASS
```

The independent oracle also ran as `python -I tests/dead_air_runes_vectors.py`
on Windows. After adding the explicit Latin/null-fields assertion, the rune
compile/run command above was rerun and printed `Dead Air runes host tests PASS`.
`git diff --check` returned 0.

During development: the initial test-first compile failed because the new
header did not exist. Python caught a wrongly hand-computed vector (corrected
`0xabeb47ee % 24` to 22). The first integrated rendering run failed because
the new test reused revision 102; giving the next frame revision 103 resolved
the test setup error. All final checks above passed.
A later attempt to reuse the temporary WSL binaries found `/tmp` artifacts
absent; the complete compile/run script was rerun successfully before committing.

## Firmware

Initial command, PowerShell from the worktree:

```powershell
pwsh -NoProfile -File apps/orcsdr-tab5/tools/build-tab5-idf.ps1
```

Exit 0, `Project build complete.` Review then corrected Othala's legs and moved
the shared selection setter into the host-testable module. An incremental build
was necessary to include those fixes. In a fresh PowerShell process:

```powershell
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
Set-Location 'F:\Ai\OrcSDR\.claude\worktrees\dead-air-dashboard-review-8c2b12\apps\orcsdr-tab5'
idf.py -B build-native-hosted3 build
```

Exit 0, `Project build complete.` Existing ESP-IDF `periph_ctrl.h:88` emitted
`-Wliteral-suffix` warnings. The focused host builds used `-Werror` and passed.
Build configuration retained the cached C6 image and no world-map image.

## Measured size comparison

The pre-change ELF/bin were copied before edits from this worktree's existing
`build-native-hosted3`. This is an observed artifact comparison, not a fresh
baseline rebuild or independent proof of the old artifact's exact source tree.
Sizes below come from actual ELF sections and DWARF struct sizes using pyelftools.

| Item | Before | After | Delta bytes |
|---|---:|---:|---:|
| Application binary | 5,376,704 | 5,379,680 | +2,976 |
| .flash.text | 2,567,648 | 2,569,056 | +1,408 |
| .flash.rodata | 2,315,212 | 2,316,516 | +1,304 |
| .eh_frame | 271,864 | 272,128 | +264 |
| .dram0.data | 67,640 | 67,640 | +0 |
| .dram1.data | 48,056 | 48,056 | +0 |
| .dram1.bss | 147,636 | 147,636 | +0 |
| .ext_ram.bss | 816,524 | 816,524 | +0 |
| State | 75,632 | 75,632 | +0 |
| Snapshot | 3,132 | 3,132 | +0 |
| VeilView | 272 | 272 | +0 |

`kElder` (768 bytes) and `kAlphabets` (32 bytes) both link into `.flash.rodata`.
The new selection fields occupy existing structure padding. No queue resizing,
new framebuffer, sprite or DMA allocation. Static/session RAM growth measured
as zero; runtime stack high-water marks and transient hardware usage were not measured.
Other linker changes: `.flash.rodata_noload` -32 and `.ext_ram.dummy` -1536
(linker padding, not allocated session buffers).

Binary SHA-256 before: `ed82210d33414a71e99c8cfc074ad7eeb31a9820d22876303a8ed105079459af`.

Binary SHA-256 after: `41535088af8c13ac85ae8cb0c5612acc646b93f7df0f958e9ac6cfd446fc4fc9`.

## Reproducible visible examples

```powershell
python -I tests/dead_air_runes_examples.py <output-directory>
```

Result: `Dead Air deterministic examples PASS`. The C++ driver invokes the real
engine, rune module and dashboard. Two fresh process runs must emit identical
primitive records. Python independently checks SHA-256 seed, quantization,
recurrence, Latin letter, and expected state/word/rune vectors. The JSON output
includes PNG and raw-RGB SHA-256 hashes. The script reads the actual GLCD Font0
data from managed M5GFX and exports its BSD license with the previews.

Each example resets intention to `DEAD AIR`, seed `545a728a`, then feeds exactly
one frame (index 0). These are synthetic reproducibility inputs, not reception evidence.

| Frequency MHz | Level dBFS | State | Latin / word | Rune index / name |
|---:|---:|---|---|---|
| 98.700000 | -31.7 | df6ddc5d | X / PRISM | 21 / ingwaz |
| 162.400000 | -45.2 | deec10d6 | O / UMBRA | 22 / dagaz |
| 146.520000 | -12.5 | de8c4331 | H / JUNCTION | 1 / uruz |

The previews crop to the overlay body because the shared header is stubbed.
Host primitive rasterization uses Pillow; it is reproducible but is not proof
of pixel-identical physical rendering, timing, touch behavior or absence of flicker.
The state and symbol values and requested glyph line endpoints come from the
production code. A live receiver's fluctuating samples will produce other frames;
reproducing a later frame requires its exact state_in, codes and frame index.

## Review and limits

A scoped read-only reviewer found and rechecked two fixes: Othala's unintended
interior crossing and a test that originally missed the actual shared selector.
The follow-up found no remaining actionable findings within that scope.
No CodeRabbit review was requested because nothing was pushed.

Device rendering, touch, authenticated serial on a device, boot/internal-pool
margin, physical flicker and RF continuity remain unverified. No hardware access.
Younger Futhark, Ogham and recent-frame runes were omitted as optional/out of
scope. The only extra firmware build was the incremental check after review fixes.
