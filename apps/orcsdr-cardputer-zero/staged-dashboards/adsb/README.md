# OrcSDR — Cardputer Zero ADS-B (staged dashboard)

A focused, **native Linux / 320 × 170 RGB565** 1090 MHz receive-only dashboard, staged for integration into the existing Cardputer Zero selector. Selector ID/title: **`adsb` / `ADS-B`**. This is not a separate app, tuner owner, map, network tracker, or audio demodulator.

## RF and decoder assumptions

- **1090000000 Hz center, 2048000 complex samples/s, unsigned interleaved CU8**. Both values must match the *applied* receiver configuration in `IqMetadata`; an FM 2.56 MS/s block is explicitly rejected.
- The shared decoder `apps/orcsdr-tab5/ui/adsb_decoder.cpp` searches a Mode S preamble and uses centers separated by **2048 milli-samples per 1 μs ADS-B bit**; this is a fixed **2.048 MS/s** detector, not an arbitrary-rate resampler. Its 16,640-sample magnitude buffer needs the staged adapter's **4096 complex-sample feeding chunks**. No IQ resampling, hidden conversion, or second copy of the native decoder is added.
- Accept only CRC-checked frames emitted by OrcSDR's portable `adsb_rx::Decoder`. Stats display preambles/candidates vs CRC-valid messages separately. A rejected candidate does not become an aircraft. RF signal is *relative detector pulse-vs-quiet magnitude*, **not calibrated dBm**.
- Exactly 32 active table slots, 6 visible rows, 90-second expiry, same-ICAO even/odd global CPR pairing within **10 seconds**, and cross-generation/discontinuity CPR invalidation. No observer location, bearing, distance, local-CPR rescue, or map is inferred. The session's unique-ICAO ledger is exact up to 256 addresses, then **capped** (not misrepresented as an exact unlimited count).

## Standalone build and focused tests

From the **repository root** on a host with a C++17 compiler and CMake:

```sh
cmake -S apps/orcsdr-cardputer-zero/staged-dashboards/adsb -B build/staged-adsb -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/staged-adsb --parallel 2
ctest --test-dir build/staged-adsb --output-on-failure
```

These commands build the staged dashboard, the **existing shared ADS-B decoder**, the synthetic input tests, and the preview only. They do not recursively build Tab5 or the entire Cardputer app. Tests are headless, need no SDR, ALSA, display, Internet access, or recordings, and use **explicit checks that remain active in Release**.

## Headless pixel previews

```sh
build/staged-adsb/orc_zero_adsb_preview --demo --screenshot build/staged-adsb/adsb-demo.ppm
build/staged-adsb/orc_zero_adsb_preview --demo --empty --screenshot build/staged-adsb/adsb-empty.ppm
build/staged-adsb/orc_zero_adsb_preview --demo --detail --screenshot build/staged-adsb/adsb-detail.ppm
```

The P6 PPM outputs are **exactly 320 × 170** and have an unmistakable `DEMO` indicator. Generated aircraft are **not live RF**. Previews cover aircraft list, selected detail, and no-aircraft state. Renderer repaints only changed widgets or bounded individual rows after initial composition; no ordinary value revision increments its `full_clears` counter. Real LCD blitting and possible physical flicker must still be validated by the coordinator.

## Controls and screen behavior

- **Up/down**: move selection among real active aircraft; selection is tracked by ICAO across expiry and oldest-first eviction.
- **Enter**: open selected aircraft detail (speed/heading/vertical rate/relative signal/global-CPR position only when known).
- **Escape**: close detail and return to list. Esc from the list belongs to the host's navigation system.
- **Space**: host dashboard selector; **P**: host palette; **Tab, G, Q** and app-level receiver controls remain host-owned. ADS-B has no volume, mute, or speaker output. The only relevant radio adjustment is optional **RF gain requested through the host**; the staged module does not implement another tuner or gain policy.

Use `Controller::navigate()` for equivalent host-callable up/down/detail/back actions. The coordinator maps physical key codes; this module does not independently read `/dev/input`.

The top strip shows live vs waiting/connecting/disconnected/error/stale or DEMO, 1090 MHz / 2.048M, active (`A`), unique (`U`) and CRC-valid (`C`) counts. Missing values render `--`; long callsigns are limited to eight characters. Only CRC-valid frames update the table.

## Optional coordinator-only bounded on-air smoke test

**With the main Cardputer application stopped** and a suitable 1090 MHz antenna, the already-existing native CLI can provide a bounded sanity check **before selector integration**:

```sh
# From an existing build with orcsdr-cardputer-zero CLI enabled:
./orcsdr-cardputer-zero --live-adsb --device 0 --freq 1090000000 --rate 2048000 --blocks 32
```

The `--blocks 32` bound makes the run terminate. Device 0 is only an example, not a discovered device. This is a decoder CLI probe, **not evidence that the staged dashboard is integrated or the LCD passes**. The coordinator should follow its current app build instructions to find the CLI executable, verify actual RTL-SDR center/rate readbacks, choose a valid dongle index, and collect concise counters. Avoid using an unsuitable antenna as negative proof of a regression.

## Current limits

This initial module intentionally has **no remote feeds, maps, aircraft database/registration lookup, GPS, MLAT, logs, speaker, WebSocket/TCP service, resampler, transmitter, or persistent state**. The host must connect its single receiver, presenter frame blit, selector and gain request. There is no guaranteed RF reception in a hardware-free sandbox. See [`INTEGRATION.md`](INTEGRATION.md) and [`HANDOFF.md`](HANDOFF.md).
