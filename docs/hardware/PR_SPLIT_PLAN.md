# Splitting PR 238 into three pull requests (plan only)

Status: **plan, nothing executed.** No branch was cut, no PR was opened or closed. It waits for the owner's yes (see "Decisions needed"). Written 2026-11-23 against `origin/main` `87045424`, from the diff of `claude/orcsdr-web-landing` (draft PR 238) against that commit: **224 files**.

## Why split

PR 238 is one large change that touches almost nothing that already exists on `main`: 114 of the 224 files are under `apps/orcsdr-p4-core`, 86 under `apps/orcsdr-web`, and only **one** is a shared Tab5 source (`apps/orcsdr-tab5/ui/home_dashboard.cpp`, 14 added lines). The repeated conflicts and breaks came from three places only: `CHANGELOG.md`, the shared UI sources `main` keeps changing, and (once) the repository's `components/` directory. A split does not remove the `CHANGELOG.md` conflicts. It makes each review smaller, lets the only shared-source change land first and be checked by the Tab5 firmware CI on its own, and lets the server (P4 Core) be reviewed before the page that uses it.

## How the two apps depend on each other

- **Build time: independent.** OrcSDR P4 Core embeds the browser bundle only if `apps/orcsdr-web/dist` exists (`apps/orcsdr-p4-core/main/CMakeLists.txt`); without it, it builds with no web UI. The browser build (`apps/orcsdr-web/web/build.sh`) compiles the shared UI sources and its own files, not P4 Core.
- **Run time: the page needs the P4 Core server.** Login, the signed routes and the IQ and audio sockets are served by P4 Core. The `tools/web_*_test.js` scripts and `tools/p4_web_auth_test.py` exercise that server, so they belong with it.
- **Documentation:** the web design docs and the board profiles doc link to each other. The documentation check (`tools/check_documentation_truth.py`) fails on a link to a file that is not in the same tree, so the merge order below matters.

## The three pull requests

### A. Shared seam (1 source file)

| Path | Change |
|---|---|
| `apps/orcsdr-tab5/ui/home_dashboard.cpp` | `ORCSDR_NO_SCREEN_BLIT` seam. The Tab5 build does not define it, so the Tab5 is unchanged. Without B or C the macro is never defined, so this is inert until they land. |
| `CHANGELOG.md` | One entry. |

Gate: the Tab5 firmware CI build must pass (it compiles this file).

### B. OrcSDR P4 Core and its tests

| Paths | Files |
|---|---|
| `apps/orcsdr-p4-core/**` (firmware, `main/`, `web/`, and `components/` with the four P4-only components) | 114 |
| `tools/p4_health.py`, `tools/p4_rtltcp_test.py`, `tools/p4_serial_cmd.py`, `tools/p4_web_auth_test.py`, `tools/p4_wifi_connect.py` | 5 |
| `tools/web_audio_test.js`, `tools/web_home_test.js`, `tools/web_iq_cycle_test.js`, `tools/web_iq_test.js`, `tools/web_maps_test.js`, `tools/web_mode_chip_test.js`, `tools/orclogin_test.js` | 7 |
| `docs/hardware/P4_BOARD_PROFILES.md`, `docs/hardware/IQ_SINKS_DESIGN.md` (P4-only, per its own header), `docs/AIS_PORTABILITY_HANDOFF.md` (changed) | 3 |
| `.github/workflows/p4-core-build.yml` (manual only, known container failure) and the three `.gitignore` lines for `apps/orcsdr-p4-core/build-*/` and `managed_components/` | 2 |
| `CHANGELOG.md` | One entry |

Gate: documentation check; P4 Core builds with the ESP32-P4-ETH overlay set; the Tab5 firmware CI build passes (the P4-only components must not be under `components/`). There is no automatic P4 compile check in CI.

### C. OrcSDR Web

| Paths | Files |
|---|---|
| `apps/orcsdr-web/**` | 86 |
| `docs/hardware/WEB_LAYER_DESIGN.md`, `WEB_API_COMMAND_MAP.md`, `WEB_HEADLESS_STATUS.md`, `WEB_PARITY_MATRIX.md`, `WebUI-Roadmap.md` | 5 |
| `CHANGELOG.md` | One entry |

Gate: documentation check; the browser bundle builds (`apps/orcsdr-web/web/build.ps1`, needs Docker). Not covered by any CI job.

## Merge order and why

1. **A first.** It is the only change to a source that already exists, and the Tab5 CI checks it.
2. **B second.** The server and its tests must exist before the page that talks to it.
3. **C last.** It links to the docs in B and needs B's server to be useful.

## How to cut it (not run)

Start each from the current `origin/main`, then copy the path sets from the landing branch with `git checkout <landing-branch> -- <paths>`. No rebase and no cherry-pick, so no history is rewritten. Split the `CHANGELOG.md` entry across the three. Delete each helper branch once its PR is merged. Close PR 238 as superseded only after the owner says so.

## Checks to run before cutting (none done yet)

- Each piece builds on its own: A on the Tab5 firmware build; B from a tree with no `apps/orcsdr-web`; C from a tree with no `apps/orcsdr-p4-core`.
- The documentation check passes in each piece: links in B's docs must not point at C's docs, and C's may point at B's.
- Each `CHANGELOG.md` entry says only what its own piece does.

## Not verified

Nothing in this plan was built or tested in isolation. The independence of B and C is read from the CMake files and the build script, not demonstrated. No image was flashed.

## Left out of every piece until the owner asks

The uncommitted COM28 (Waveshare ESP32-P4-WIFI6-POE-ETH) Wi-Fi notes in `docs/hardware/P4_BOARD_PROFILES.md` and the logging change in `tools/p4_wifi_connect.py`. They belong with B if included.

## Decisions needed from the owner

1. Split into A, B and C, or keep one PR?
2. Land A on its own (inert until B and C), or fold it into B?
3. Close PR 238 once the pieces are open?
4. Include the COM28 Wi-Fi notes and logging change in B?
