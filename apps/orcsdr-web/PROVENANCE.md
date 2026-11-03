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

## The previous bundle

`F:\Ai\_orcsdr_web_ui\m5gfx-web-mirror-67e535e` holds only gzipped build outputs and `SHA256SUMS.txt`: no sources, licence or history. It is no
longer a build input.
