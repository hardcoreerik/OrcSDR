# AIS portability handoff (OrcSDR P4 Core and OrcSDR Web)

Status: **analysis only. No P4 Core or Web build of this branch has been run.** Nothing here is evidence that either target compiles or receives AIS. The owner of `claude/orcsdr-web-landing` (draft PR 238) makes the changes; this branch does not touch `apps/orcsdr-p4-core` or `apps/orcsdr-web`.

## Dependency map of the AIS sources

| Module | Dependencies | Portable as is |
|---|---|---|
| `ais_receiver`, `ais_receive_pipeline`, `ais_protocol`, `ais_station_store`, `ais_binary_app`, `ais_snapshot_adapter`, `ais_map_policy` | C++17 standard library only | Yes (compile on host, build for wasm) |
| `ais_radio` | standard library; `esp_heap_caps.h` only under `ESP_PLATFORM` | Yes, with hooks |
| `ais_app` | `screen_controller.hpp`, `ais_dashboard.hpp`; `esp_heap_caps.h` under `ESP_PLATFORM` | Needs `screens::Id::ais` and the screen controller |
| `ais_dashboard` | M5Unified (`M5.Display`, `M5Canvas`), M5GFX, `dashboard_audio_control.hpp` (header drawing), `focus_nav.hpp`, `acars_dirty_rects.hpp`, `map_view.hpp` (ESP only) | **No.** Draws through M5GFX directly; the host build uses a recording stub (`tests/acars_ui_stubs`) |

## Per-target work (for the platform owner)

1. Add to the platform source list: `ais_receiver.cpp ais_receive_pipeline.cpp ais_protocol.cpp ais_station_store.cpp ais_binary_app.cpp ais_snapshot_adapter.cpp ais_radio.cpp ais_app.cpp ais_dashboard.cpp` (paths `apps/orcsdr-tab5/ui/`).
2. `ais_dashboard.cpp` needs a display layer that provides what it uses from M5GFX. It draws in the 1280x720 logical space with `M5.Display` and two PSRAM sprites; a P4 Core display build can supply that through its M5GFX-compatible layer, but the browser needs a canvas adapter (or a separate renderer driven by the same `ais_ui::Snapshot`).
3. Provide the `ais_radio::Hooks` (`device_ready`, `tune`, `release`, `tuned_center`, `now_ms`, optional `micros`, `lost_samples`, `remote_source`) from that platform's radio session. The contract is in `ais_radio.hpp`; the Tab5 versions are in `open_ais_dashboard()` in `ui/main.cpp`.
4. Feed `ais_radio::offer_iq` from the platform's IQ consumer only for a 240 kS/s stream centred on 162.000 MHz, and call `note_discontinuity()` on any source reconnect.
5. Headless P4 Core: the decoder and station store run without a display. Expose `ais_radio::status()` and the `ais_ui::Snapshot` fields (targets, messages, counters) through the web interface instead of the dashboard.
6. Browser: a wasm compile of the portable modules proves syntax only. **Live AIS decoding in a browser additionally needs a 240 kS/s 162 MHz IQ source** (an rtl_tcp or IQ transport the web architecture does not yet offer for this band). Do not describe a successful wasm build as live reception.
7. Capability reporting: distinguish `unsupported` (platform cannot supply 240 kS/s at 162 MHz) from `disconnected` (capable source, nothing attached). `ais_radio::Link` already separates `no_receiver` from `wrong_stream`.

## What reading the P4 Core source (landing branch facf7ce9, read only) shows

- P4 Core compiles an explicit subset of `apps/orcsdr-tab5/ui/` sources plus its own per-dashboard wrapper (`screen_acars.cpp` is the model): AIS needs a `screen_ais.cpp` wrapper (enter/leave/tick/tap, `ui_support::claim_screen(orcsdr::screens::Id::ais)`) and the source-list entries above in `apps/orcsdr-p4-core/main/CMakeLists.txt`.
- `dashboard_registry.cpp` is shared, so the AIS entry already exists in P4 Core's registry. Opening it today only logs "dashboard N is not available on this target yet" (`home_app.cpp:228`); nothing crashes. Add `ais` to that target's availability when its wrapper exists.
- `ais_dashboard.cpp` should join the files built with `ORCSDR_LOVYANGFX_IS_SCALED;ORCSDR_NO_RAW_BUFFERS` (as `acars_dashboard.cpp` is). Its frame diff reads `getBuffer()` and already falls back to a whole-body paint when that is null, which is what the shim returns, so no source change is expected; this is unverified until it is compiled.
- The dashboard calls `maps::draw_region` / `maps::project` (`map_view.hpp`); P4 Core has `map_view.cpp` and `map_service_p4*.cpp`, so the map page should link, again unverified.

## Known shared-source issues the platform builds may hit

- `ais_dashboard.cpp` and `ais_app.cpp` use the namespace `orcsdr::screens` and `Id::ais`; the other platform's `screen_controller` must include it.
- `ais_dashboard.cpp` includes `esp_attr.h` (for `EXT_RAM_BSS_ATTR`) only under `ESP_PLATFORM`. A non-ESP build gets a plain static.
- The dashboard text says "TEST REPLAY DATA - NOT LIVE RF" when `Snapshot::test_replay` is set; it contains no Tab5-specific wording.

## Verification to run in a throwaway worktree (not yet done)

Combine this branch with the latest `claude/orcsdr-web-landing`, build P4 Core for Waveshare ESP32-P4-ETH and the wasm bundle with the documented scripts, flash nothing, commit nothing, and classify each failure as a dashboard portability defect, a missing source-list entry, a missing browser source capability, or an unrelated existing failure.
