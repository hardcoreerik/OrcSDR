# FT8 whole-world OrcMaps integration — bounded sandbox evidence (2026-10-19)

## Scope and source gate

- Repository: `hardcoreerik/OrcSDR`; verified source branch `codex/maps-integration-completion` at `af877ed7189d99f99873f61f5be3995ad893db3b`. GitHub compare `af877ed7...codex/maps-integration-completion` returned **identical**, 0 commits.
- Review branch: `codex/ft8-orcmaps` (based on that exact commit). No integration branch or main mutation. This is source/host work only; **no COM17/COM39 access or flash**.
- Read current plan `docs/superpowers/plans/2026-10-18-orcmaps-orcsdr-quality-and-delivery.md` and inspected FT8, map service, map pipeline, regional consumers, pinned OrcMaps viewport/geo source. No `AGENTS.md` exists at the repository root, `apps/orcsdr-tab5`, `apps/orcsdr-tab5/ui`, or affected plan directory on this branch.
- Sandbox direct Git could not fetch (`git ls-remote https://github.com/hardcoreerik/OrcSDR.git refs/heads/codex/maps-integration-completion` returned `Could not resolve host: github.com`). Repository inspection, commits and branch creation used the connected GitHub API. Entire native repository and managed build dependencies were **not** available to compile.

## Projection resolution before dashboard edits

FT8 formerly drew compiled Natural Earth outlines in a full-world equirectangular map, with all markers and 30-degree Maidenhead lines projected using `world_compat_project`. The pinned OrcMaps viewport is Web Mercator (up to ±85.0511287798066 degrees) and has one-world X wrapping; blindly replacing the old pixels creates incorrect overlays and loses both poles. The current shared source explicitly identifies its world overview as Mercator with polar caps excluded.

The bounded consumer-only solution requests the **embedded** `firmware-world` identity through `maps::draw`, using a fixed, centred 512×512 Mercator viewport (`zoom=1, tile_size_px=256`, or a source coarser zoom with identical effective world pixels). A separate FT8-owned PSRAM compositor remaps each output row by the exact spherical Mercator Y formula used by `orcmap::LatLonToTileCoord`, and X across exactly one longitude revolution. The radio overlays use the destination's exact equirectangular transform, including the dateline seam and both poles. The ±85.0511° to ±90° caps show **grid only** with an explicit polar caption; no invented coastline or projection mismatch. No OrcMaps engine or shared map service changes are needed.

Zoom remains fixed at whole-world scale. The shared map pipeline's existing `viewport.zoom >= 6` threshold for geographic city labels is unchanged; the FT8 independent NVS setting can be toggled, but this view does not suddenly gain city names. No SD, network, interactive pan or zoom required.

## Source changes

- `ui/ft8_dashboard.cpp`: offscreen map compositor, transparent pending/last-frame and grid-only fallback decisions, matched Maidenhead/station projection, label button and touch, published map revision handling, owned-sprite exit cleanup, unchanged recent-grid display.
- `ui/ft8_map_source.{hpp,cpp}`: narrow bridge to the already-pinned `maps::draw` / `maps::revision` APIs; immutable labels passed to worker; always selects `firmware-world` without a regional pack dependency.
- `ui/ft8_world_projection.hpp`: host-testable whole-world projection + row inverse mapping.
- Main CMake removes the obsolete FT8 outline renderer/data from firmware compilation and registers the adapter; legacy files remain in the tree for other source/history needs.
- Existing FT8 host harness gains worker frame, persisted-label-failure, update and sprite-allocation stubs; runner adds geometry regression.

## Executed tests / honest limits

From this sandbox, with local copies of the new pure-math header and new host regression:

```sh
cd /mnt/data/ft8-orcmaps-sandbox
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -I apps/orcsdr-tab5/ui tests/ft8_world_projection_tests.cpp -o ft8_world_projection_tests
ASAN_OPTIONS=detect_leaks=1 ./ft8_world_projection_tests
```

**PASS:** `ft8_world_projection_tests: PASS (full 824x420 pixel-centre sweep, seams, poles)`. Tests include equator, both ±180° representations, near-dateline sides, N/S poles and invalid inputs. For every Mercator-covered destination pixel, projected marker centre agrees with destination and Mercator source sampling to within 1 source pixel. For true polar rows source sampling is rejected. This does **not** establish that the embedded archive renders on hardware.

Additional regression cases have been **added but not executed here**: dashboard map pending-to-ready, worker unavailable after an old frame, persisted labels toggle and failure, request-label independence from other dashboards, and failed PSRAM sprite allocation (see `tests/ft8_dashboard_tests.cpp`, `tools/test-ft8.sh`). Existing `apps/orcsdr-tab5/tests/maps/map_preferences_tests.cpp` already exercises independent picker/LoRa/ADS-B/FT8 preference keys, failed NVS save and reload; no rerun in this sandbox. Integrated `bash tools/test-ft8.sh`, actual P4 syntax/native link, font render and COM17 visual/performance/SD/radio tests **not run** because full source/dependencies and hardware are unavailable.

## Remaining risks / COM17 handoff checklist

1. Fetch this branch on the local OrcSDR checkout; **do not merge to main**. Confirm dependency lock matches the existing pinned OrcMaps commit and COM17 serial **`30:ED:A0:E2:E6:95`**. Preserve NVS, SD pack contents, recovery artifacts and C6 firmware; COM39 is reserved for Dead Air.
2. Run `bash tools/test-ft8.sh` including normal and ASan/UBSan FT8 dashboard and geometry suites; run the focused map preference tests in the configured OrcMaps host build. Resolve any failures before device work.
3. Native ESP-IDF build with existing lock, no managed dependency upgrade; inspect flash/PSRAM headroom and linker symbols, confirm legacy FT8 data/renderer are no longer linked.
4. With **no SD / Wi-Fi disabled**, open FT8/FT4/JS8 map: embedded Natural Earth appears (if provisioned), distinct 30° whole-world grid lines line up with coastlines, polar rows are visibly grid-only; test Alaska, New Zealand, equator, ±179.9° and stations at high latitudes; compare station/receiver markers to known Maidenhead grid centres.
5. Toggle FT8 **LABELS ON/OFF** (190×44 px hit target) and return/reboot. Verify persistence, independence from Picker/LoRa/ADS-B, credit text, and no city-name violation below zoom 6; overlays survive toggle.
6. Trigger decode updates while world render is **pending** and after completion; verify no repeat blanking, old-frame status truthful, markers recompose with matched basemap, and no periodic retry loop. Exercise no world partition and forced low-memory paths where safely possible; verify grid-only status and no crash.
7. Repeat basic map picker, LoRa/ADS-B basemap, retained-SD hash/readback, active radio and Home/Settings transitions. Record graphics errors, memory high-water/alloc errors and P4 timings **as measurements**, not inferred host numbers. No flashing is authorized in this sandbox; local device owner decides.
