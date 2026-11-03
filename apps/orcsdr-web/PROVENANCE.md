# Provenance of apps/orcsdr-web

Recorded 2026-11-02 on branch `claude/web-in-orcsdr`. The owner decided the same day that the OrcSDR-Headless web work moves into OrcSDR
and that nobody works in the Headless repository any more. It was read only for this import.

## Source

| | |
|---|---|
| Repository | OrcSDR-Headless (`https://github.com/hardcoreerik/OrcSDR-Headless`), owner's local clone read with `git archive` |
| Branch and commit | `feat/m5gfx-web-mirror`, tip `67e535e` (2026-10-27, "FM screen: a fresh start runs at 2.56 MS/s, not whatever rate the idle receiver was left at") |
| Authorship | All 43 commits on the branch are by hardcoreerik (the repository owner). No other author, no `Co-authored-by` line. |
| Licence of the imported code | AGPL-3.0, the same as OrcSDR, chosen by the owner on 2026-11-02. The Headless repository had not chosen one for its new code. |

## What was imported, and where it went

| Headless path | OrcSDR path | Change |
|---|---|---|
| `platform/web/` (controllers, stubs, page in `app/`, `build.sh`, `build.ps1`, `pack.ps1`, badge) | `apps/orcsdr-web/web/` | Build scripts: paths changed to this repository, output goes to `apps/orcsdr-web/dist/`, M5GFX pinned by tag **and** commit. Sources otherwise byte-identical. |
| `core/decoders/` (`include`, `src`, `shim_host`, `shim_portable`, `wasm`, `host`, `sources.sh`, `README.md`) | `apps/orcsdr-web/decoders/` | Paths changed so it compiles the device sources in `apps/orcsdr-tab5/ui` in place. |
| `core/ui/` (226 files) and `core/decoders/vendor/orcsdr/` (13 files) | **not copied** | They are copies of `apps/orcsdr-tab5/ui`. Checked 2026-11-02 against this branch: 238 files are byte-identical, the 239th is `orc_badge.hpp`. The web build now compiles `apps/orcsdr-tab5/ui` directly. |

Not imported: the Headless firmware (`main/`, `components/`), which descends from the separate OrcNode project; its tests, evidence, research and
scratch files; `docs/`.

## The one difference from the device sources

`apps/orcsdr-tab5/ui/orc_badge.hpp`: the Headless copy replaced the linker end symbol (`asm("_binary_orc_badge_104_rgb565_end")`) with a fixed
size, because WebAssembly has no linker end symbol. Here the same change is made as `#ifdef __EMSCRIPTEN__`, so device builds see the
original line unchanged.

## Third-party code

M5GFX is not stored in this repository. `web/build.ps1` fetches tag `0.2.25` (commit `ad9b814264d4e2000e9f30070002310bbccaffc9`) and refuses any
other commit. Its compiled parts are in `core.wasm`. See `THIRD_PARTY_NOTICES.md`. The device builds use M5GFX 0.2.27 through
the component manager, so the browser and the devices do not draw with the same M5GFX version (kept at 0.2.25 to reproduce today's page).

## LilyGO integration checkpoint — 2026-11-03

This source tree was selectively imported from OrcSDR commit
`de374cc3b6b19225525d8c2cfd84f3fee2f3fcaa` into
`codex/p4-display-integration`. The P4 login script/injector and focused signing
tests came from `3fd448c39e5478b0f7a961ca1063474622875d6d`. No old firmware,
duplicate dashboard source tree, previous gzipped bundle, or private device
snapshot was copied. Existing device dashboards remain the build inputs.

The current-source decoder WASM build passed in `emscripten/emsdk:3.1.74`.
`node tools/test_web_decoder_wasm.js` passed metadata and create/reset/destroy
checks for all eight decoder types. This ABI smoke test is not reception or
protocol replay parity. `node tools/orclogin_test.js` passed its crypto vectors.
The UI WASM build failed because its old source list references
`offline_map.cpp`, retired by the current shared UI in favor of OrcMaps.
The next integration must adapt the browser map service to that current
architecture, rather than restoring the retired renderer or substituting a
mock map. No browser page/package success is claimed at this checkpoint.
Local logs: `.local/web-decoders-build.log`, `.local/web-bundle-build.log`.

## Historical bundle

`F:\Ai\_orcsdr_web_ui\m5gfx-web-mirror-67e535e` holds only gzipped build outputs and `SHA256SUMS.txt`: no sources, licence or history. It is no
longer a build input.
