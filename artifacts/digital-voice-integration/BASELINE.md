# Baseline (recorded 2026-11-22/23)

| Item | Value |
|---|---|
| origin/main | 870454241c0d58af246755a5263a8e3ead6835c2 (merge of PR 265) |
| origin/feature/dmr-digital-voice-foundation | b3b781287eaaa8bd0da3d3a780c345709cbb0343 |
| merge-base | df1fd7ecc9a52be8807b2ddb39fe57b53ea14f00 |
| divergence | 69 on main only, 124 on DV only (`git rev-list --left-right --count`) |
| local branch | `local/digital-voice-integration` (this worktree, never pushed) |
| host toolchain | WSL Ubuntu 24.04, g++ 13.3.0, cmake 3.28.3 (no native Windows compiler) |
| firmware toolchain | ESP-IDF 5.5.4 at C:\Espressif, `apps/orcsdr-tab5/tools/build-tab5-idf.ps1` |

Both reference SHAs from the assignment matched the fetched remotes exactly.

## Fact that changes the plan
`main` is NOT "main.cpp-free". `apps/orcsdr-tab5/ui/main.cpp` is 21,335 lines and still holds
the scan_engine instance (line ~1376), the scan service loop (~11060-11290) and the dashboard
open/draw/touch switches. The radio `Owner` enum (radio_session.hpp) has no digital_voice value.
All wiring must therefore be thin hooks in main.cpp plus a new module.
