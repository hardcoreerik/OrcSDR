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

This script activates ESP-IDF 5.5.4, regenerates the per-build sdkconfig from
`sdkconfig.defaults`, and runs `idf.py reconfigure` so managed components are
present. It then applies the local M5GFX Tab5 page-flip patch, validates the
required sdkconfig values, and runs `idf.py build`. This order supports a fresh
checkout or worktree and prevents component resolution from overwriting the
patch before compilation.

### Flash

```powershell
idf.py -B build-native-hosted3 -p <PORT> flash
```

Only after explicit hardware authorization. `<PORT>` is whatever serial
port the Tab5's native USB Serial/JTAG connection enumerates as on your
machine (e.g. `COM17` on Windows, `/dev/ttyACM0` on Linux) — it's
machine-specific, not a fixed value, and can change if other USB serial
devices are plugged in. Always pass it explicitly; do not rely on port
auto-detection, which can select a different attached device if more than
one is present.

If you keep a fixed bench assignment, record it in a local, gitignored
file (e.g. `.local/dev-environment.local.md`, under the existing
`.local/` gitignore rule) rather than here — this file is read by
contributors and coding agents on every machine, so it must stay
machine-agnostic. See
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
