# AIS integration checkpoint (resume here)

Branch `claude/ais-integration-hardware-79f921`, based on `origin/main` df1fd7ec (rtl_tcp PR #240). Not pushed as a PR; nothing merged. Source branch `origin/chatgpt/ais-dashboard-20261106` is preserved untouched.
Last updated 2026-11-10. Evidence: [docs/evidence/ais/2026-11-10-tab5-replay.md](evidence/ais/2026-11-10-tab5-replay.md).

## What exists now

| Layer | Files | State |
|---|---|---|
| DSP and framing | `ui/ais_receiver.*` | Per-burst carrier tracking, mirrored-history FIR. Host stress floors met; Tab5 duty about 32% of one core at 240 kS/s (noise input) |
| Parse and state | `ui/ais_protocol.*`, `ais_station_store.*`, `ais_binary_app.*` | Types 1-3, 4, 5, 9, 18, 19, 21, 24, 27, 28 decode fields; 6/8/25/26 envelope + 3 DAC/FI apps |
| Radio adapter | `ui/ais_radio.*` | New. Asks for 162.000 MHz / 240 kS/s on the selected source, DSP-task `offer_iq`, own epoch, PSRAM state (about 70 KB), 500 ms publish |
| App glue | `ui/ais_app.*` | New. Screen lifecycle, UI snapshot (about 17 KB PSRAM), link state |
| Dashboard | `ui/ais_dashboard.*` | Seven pages. TEST REPLAY banner when the bench marks data as replayed |
| `main.cpp` hooks | about 90 lines | open/close/tick/draw/touch, one IQ dispatch line, rtl_tcp discontinuity, serial commands |
| Tools | `tools/ais_make_iq.py`, `ais_decode_cu8.cpp`, `ais_check_replay.py`, `ais_tab5_replay_test.py`, `tests/ais_receiver_stress.cpp` | See evidence doc |

## Signal path as built

`RTL-SDR USB | rtl_tcp` -> `rtl_dsp_task` (core 1) -> `ais_radio::offer_iq` (only when band is browse, AIS running) -> `Pipeline::offer` -> parser -> station store -> every 500 ms `populate_targets` + journal copy into a PSRAM `shared` snapshot (spin-flag guarded, never blocks the DSP task) -> UI task `ais_app::tick` copies it -> `ais_ui::update`.

- IQ buffer owner: the receiver ring slot, borrowed for the duration of `offer_iq`. AIS copies nothing.
- Mutable decoder state: DSP task only. UI never sees the store.
- Epoch (`source_generation` to the pipeline) advances on: accepted tune, `rtl_tcp_sink_discontinuity` (flag only), lost-sample counter movement. The radio-session generation is NOT used: it does not change on reconnects, dropped buffers or hot retunes.
- Wrong rate or centre: counted and refused, never resampled. A re-tune is requested after 3 s.

## Decisions and constraints to keep

- AIS shares the `browse` band with ACARS, SSTV, FT8 (VHF) and Dead Air. Opening any of them closes AIS (hooks in `open_*_dashboard` and `open_dashboard`).
- No internal-RAM fallback for the 70 KB decoder state. If PSRAM allocation fails the link shows `no_memory`.
- `dashboards::Id::ais` is appended last so stored recent-dashboard ids keep their meaning.
- Synthetic test data is only enabled by `RTL_AIS TEST ON` (authenticated serial). Production never fabricates data.
- Replay is network only. Never transmit simulated AIS.

## Open items (ordered)

1. USB soak and real-antenna reception (nothing but noise received so far; the lab sees no ships).
2. Compare identical real IQ against an independent decoder (AIS-catcher / gr-ais). Only pyais cross-checks of generator output exist.
3. Setup-page toggles return actions that nothing applies (AIS 1/2 enable, NMEA, journal, receiver).
4. Events page has no data source. Message types 12/14 (safety text) and 11/15/16/17/20/22/23 are header only.
5. Map: marker invalidation, overlap selection, antimeridian, OrcMaps overlay API not evaluated; map redraws the basemap on each paint.
6. Regression runs of FM, FT8, ADS-B and rtl_tcp modes after AIS (only source switching and AIS entry/exit were exercised).
7. Portability builds (P4 Core, Web wasm): see [AIS_PORTABILITY_HANDOFF.md](AIS_PORTABILITY_HANDOFF.md). Not built.
8. Possible filter speed-up: FIR loop measured 250 ms per second of IQ on the P4 whatever the C source shape; inspect the generated code before optimising further.

## Reproduce

```bash
python tools/ais_make_iq.py --out ais_fleet.cu8 [--origin LAT,LON]
U=apps/orcsdr-tab5/ui
g++ -std=c++17 -O2 -I$U tools/ais_decode_cu8.cpp $U/ais_radio.cpp $U/ais_receiver.cpp $U/ais_receive_pipeline.cpp \
    $U/ais_protocol.cpp $U/ais_binary_app.cpp $U/ais_station_store.cpp $U/ais_snapshot_adapter.cpp -o ais_decode_cu8
./ais_decode_cu8 ais_fleet.cu8 > host_decode.txt
python tools/ais_check_replay.py ais_fleet.cu8.json --host host_decode.txt [--pyais]
python tools/rtl_tcp_replay_server.py --bind <PC LAN address> --port 1235 --iq ais_fleet.cu8 --rate 240000 --gap 1
python tools/ais_tab5_replay_test.py --port COM17 --server <PC LAN address>:1235 --manifest ais_fleet.cu8.json --host host_decode.txt --out out_dir
```

Serial: `RTL_UI OPEN AIS`, `RTL_AIS STATUS`, `RTL_AIS TEST ON|OFF`, `RTL_AIS BENCH` (stop the radio first).
Build: `apps/orcsdr-tab5/tools/build-tab5-idf.ps1`; flash app only: `idf.py -B build-native-hosted3 -p COM17 app-flash`.
