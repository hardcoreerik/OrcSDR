# OrcSDR × OrcMaps integration worklog — 2026-10-17

**Scope: Maps only. No general `main.cpp` refactor. No full firmware/hardware verification yet.**

## Source baseline

- OrcSDR `main` at `fba861e36970357e0a005c03d666250b09096cb6` (2026-10-17).
- OrcMaps `main` at `14cdd418fe9101d56ea904eaa8c4da695a7fed9d` (2026-09-28).
- OrcMaps v0.2.1 release commit `882093ee6ec133cc66afcfce7de4884e91f5d15f`.
- Historical `claude/orcmaps-integration` diverged (2026-10-17 GitHub comparison: main ahead 340, old branch ahead 15). Do not wholesale merge its old `main.cpp`, partitions, or dependency manifest.
- Current main pins `esp-rtl-sdr` v0.9.3 commit `31a159df4f006a837d5041029bc6d1bc63520234`; old integration branch pins an obsolete driver SHA. Preserve current main's pin.

## Verified ownership map

| Consumer | Current map implementation / evidence | Migration |
|---|---|---|
| ADS-B | `ui/adsb_dashboard.cpp` calls `offline_map::draw_base` | Replace ORCMAP1 basemap via shared OrcMaps service; preserve aircraft overlays |
| LoRa | `ui/lora_dashboard.cpp` calls `offline_map::draw_base/project` | Share basemap and projection; keep only verified coordinate nodes |
| FT8 | `ui/ft8_dashboard.cpp` draws separate world polygons; `docs/ft8/CHANGELOG.md` says theme match only | Replace projection/world drawing once OrcMaps parity and code-size gates pass |
| Airband | `ui/airband_dashboard.cpp` uses canonical receiver location, no independently verified map screen | Reuse location, do not invent a separate map |
| Weather | PR #173 described maps/location reuse | Verify implementation/merge status before planning conversion |
| Location picker | Historical `setup_map_picker` on divergent branch | Salvage model; replace blocking framebuffer/touch loop before integration |

Current map is **not** OrcMaps PMTiles: `ui/offline_map.hpp` pins an ORCMAP1 index at `/orcsdr/data/lane_county_map.idx`. OrcMaps uses `/orcmaps/<name>.pmtiles` + `.manifest.json` and optional `.sha256`. Never rename one format to the other or overwrite legacy user data.

## Why Lane County appeared on a fresh device

`ui/catalog_sync.cpp` hard-coded `LANE COUNTY MAP` in the five built-in catalog titles. Actual installed-state detection separately checks the SD path. A clean COM39 could show the catalog row without having downloaded data; only read-only SD+serial inventory can prove pack presence.

## Committed on this branch (source-level only)

1. Built-in regional label changed to **REGIONAL MAP (LEGACY)**, preserving `lane_county_map` ID and SD path.
2. Settings catalog view expanded from five to all sixteen available catalog slots. `global_settings_state()` already copies `std::size(catalog_packs)`; no `main.cpp` edit required. The app's `g_settings_snapshot` lives in PSRAM.
3. Settings Data & Maps now has five-row PREV/NEXT pagination of nonempty IDs, with touch actions passing *original* catalog indices, not page-relative indices. Removal confirmation resets on page/catalog changes.
4. Added `ui/map_catalog_paging.hpp`, `tests/map_catalog_paging_tests.cpp` and `tools/test-map-catalog-paging.sh`. The standalone C++ helper passed local g++ C++17 ASan/UBSan tests for zero, five, six, sixteen and sparse slots. Script writes `map-catalog-paging-test.log` in the directory it is launched from.

**Not claimed:** full ESP-IDF compile, firmware size, on-device visual/touch test, SD inventory, serial acceptance, flashing or merge.

## OrcMaps dependency and renderer safety

Read `OrcMaps/docs/ORCSDR_INTEGRATION.md`, not only its outdated `STATUS.md`. **Never use OrcMaps v0.2.0 on P4:** ESP32 ROM miniz symbol collision caused heap corruption at first inflate; v0.2.1 fixed it and added `tools/check_miniz_symbols.py` target link gate. The world preview pack is 871,343 bytes and requires a separate ~1 MiB read-only flash partition, not the app binary. Packaging into M5Burner/full firmware images must be validated, with optional absent-world fallback. Do not copy the old integration branch's packaging without verifying current partition/flash constraints.

OrcMaps v0.2.1 offers stable `LocationPicker`, `Viewport`, `PartitionByteSource`, `DiscoverPacks`, streaming `PmTilesReader::StreamTile`, and optional M5GFX `DisplayTarget`; it **does not yet** provide a complete shared `MapEngine`, cache, overlay framework, text labels, or on-device SHA verification. Favor one reusable reader/scratch per active archive, then a thin OrcSDR adapter rather than reimplementing projection in each dashboard.

OrcMaps PR #4 (`claude/polygon-rings`) corrects missing world multipolygon parts/holes, reproduced in the setup picker. Host tests passed; PR was open/unmerged and **not hardware-verified** on 2026-10-17. Resolve upstream and accept with a golden image plus Tab5 test.

## Wizard caution

The historical branch has `setup_wizard.*`, `setup_wizard_store.*`, `setup_map_picker.*` and host tests, but prototype `run_first_run_setup()` skips welcome/network, blocks on the map picker and silently completes maps. `setup_wizard_store::Load()` treats missing completion keys as requiring setup (including legacy installs), and `Save()` writes `adsb_loc_set` from the wizard record. Before porting, define safe upgrade migration and prevent reruns/skips/cancels from clearing a valid receiver location. Keep `ScreenController` sole framebuffer owner and radio/worker servicing alive.

## Next local acceptance sequence

1. Checkout branch, review diff, run `apps/orcsdr-tab5/tools/test-map-catalog-paging.sh` from the desired evidence directory. Capture log.
2. Build Tab5 under ESP-IDF 5.5.4; size and documentation gates; visual Settings pages 1..4 and install/remove correct index. No map downloads without approval.
3. Identify COM17 vs COM39 and inspect firmware/hash, SD data, catalog, NVS read-only; do not erase/flash/format or touch COM17 recordings without approval.
4. Design production OrcMaps map-session adapter; stage and test the v0.2.1 world partition and polygon fix, streaming benchmark and pack discovery.
5. Port wizard model/store after migration corrections, integrate true event-driven first-run and Settings rerun, then migrate ADS-B → LoRa → FT8 as separately tested map consumers.
6. Profile real ESP32-P4 under radio load: frame latency, SD reads, internal DMA largest block, PSRAM, IQ/audio drops, USB errors and watchdog.

Never conflate a catalog listing with an installed pack or claim hardware test from host-only output.
