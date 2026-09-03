# Development environment

Reproducible setup notes for building and flashing OrcSDR firmware. This
documents what a machine needs beyond what's tracked in the repo (Python
`requirements*.txt` files only cover standalone tool scripts, not the
firmware toolchain itself).

## Tab5 firmware (apps/orcsdr-tab5)

Native ESP-IDF build; PlatformIO is not a supported build or flash path.

| Requirement | Version / path | Notes |
|---|---|---|
| ESP-IDF | 5.5.4 | Installed via Espressif's standard installer to `C:\Espressif\frameworks\esp-idf-v5.5.4` (Windows path shown; adjust for other OSes). Do not substitute a different ESP-IDF version — component pins in `apps/orcsdr-tab5/dependencies.lock` target 5.5.4. |
| IDF Python env | `C:\Espressif\python_env\idf5.5_py3.14_env` | Created by the ESP-IDF installer alongside the framework; not a project-level virtualenv. |
| Target toolchain | ESP32-P4 (riscv32-esp-elf), installed by the ESP-IDF installer | Do not override the platform toolchain. |
| M5Unified / M5GFX | 0.2.20 / 0.2.27 | Pulled as ESP-IDF managed components (`managed_components/`), pinned by `apps/orcsdr-tab5/main/idf_component.yml` and `dependencies.lock`. |

### Build

```powershell
cd apps/orcsdr-tab5
.\tools\build-tab5-idf.ps1
```

This script activates the pinned ESP-IDF environment, applies the local
M5GFX Tab5 page-flip patch, regenerates the per-build sdkconfig from
`sdkconfig.defaults`, and runs `idf.py build`.

**Known gotcha — first build in a fresh checkout/worktree:** the script
applies `tools/patches/m5gfx-tab5-pageflip.patch` to `managed_components/`
*before* running `idf.py reconfigure`. On a machine (or worktree) that has
never fetched `managed_components/` yet, the patch step has nothing to
patch and fails outright. Even if you pre-populate `managed_components/`
(e.g. copied from another checkout) so the patch step succeeds, the
subsequent `idf.py reconfigure` component-manager step can still
re-resolve/overwrite `managed_components/m5stack__m5gfx` and silently drop
the applied patch, producing build errors like `'struct
lgfx::v1::Panel_DSI' has no member named 'getPanelHandle'`.

Workaround until the script is fixed: run `idf.py reconfigure` once first
(via `. tools\build-tab5-idf.ps1`'s underlying `idf.py` invocation, or
manually after activating the IDF environment) so `managed_components/` is
fully fetched, then apply the patch
(`tools\apply-m5gfx-tab5-pageflip.ps1`), then build with `idf.py build`
(skip `reconfigure` on that run so the patched files aren't re-fetched).

### Flash

```powershell
idf.py -p <PORT> flash
```

Only after explicit hardware authorization. `<PORT>` is whatever serial
port the Tab5's native USB Serial/JTAG connection enumerates as on your
machine (e.g. `COM17` on Windows, `/dev/ttyACM0` on Linux) — it's
machine-specific, not a fixed value, and can change if other USB serial
devices are plugged in. Always pass it explicitly; do not rely on port
auto-detection, which can select a different attached device if more than
one is present. See
[`apps/orcsdr-tab5/README.md`](apps/orcsdr-tab5/README.md) for file-transfer
and splash-asset details.

## Standalone Python tools

Install into any Python 3.10+ environment as needed per script:

| File | Used by |
|---|---|
| `requirements.txt` (repo root) | `install-orcsdr.ps1` people-installer (talks to the Tab5 over serial after flash) |
| `tools/requirements-docs.txt` | Documentation build tooling |
| `tools/requirements-help-media.txt` | Help-media generation scripts |
| `tools/requirements-lora.txt` | LoRa capture/decode tooling under `.local/lora-*` |

## PR workflow

- Open PRs from a branch created off `origin/main`, in its own git worktree.
- CodeRabbit does not auto-review repos with fewer than 10 stars; trigger
  it manually by commenting `@coderabbitai review` on the PR. It reviews
  each pushed commit only once — comment `@coderabbitai review` again
  after pushing any follow-up commit to get it re-reviewed.
