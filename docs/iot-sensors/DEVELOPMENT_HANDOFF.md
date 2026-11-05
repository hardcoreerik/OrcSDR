# IoT / Sensor Discovery — implementation handoff

Status: **foundation only** (2026-11-04). This branch does NOT yet receive, demodulate, or
identify live RF signals, and does NOT yet register a usable M5GFX or browser dashboard.
A radio signal cannot be identified solely by seeing energy near a known frequency.

## Product contract

From Home, an eventual **IOT / SENSORS** dashboard should offer **SCAN**,
**PAUSE**, **STOP ON CONFIRMED SENSOR**, and **SAVE UNKNOWN**. It should:
1. Check tuner frequency capabilities; never pretend an unsupported band can be scanned.
2. Acquire the existing `radio_session` owner, without stealing an active session.
3. Cycle **candidate frequency centers**, leaving enough dwell time to catch burst sensors.
4. Run compatible *passive* OOK/ASK and FSK/GFSK decoders over the same received burst,
   not hundreds of separate retunes for each protocol.
5. Use separately tested LoRa demodulation where the receiver and implementation support it.
   LoRa/LoRaWAN are not interchangeable with simple OOK/FSK.
6. Show **Signal detected** → **Protocol candidate** → **Identified device**
   → **Decoded measurement**, exposing the evidence at every step.
7. Retain the discovered device table while scanning other frequencies. Support archive
   replay and a capped opt-in raw-IQ evidence store for unknown bursts.
8. Never automatically transmit, pair with equipment, issue commands, or bypass security.

The UI must match `docs/UI_RENDERING_STANDARD.md`: static chrome once, dirty-region
updates only, data published from non-UI work, one draw owner, no periodic full-screen
clears. Use a Model / Controller / M5GFX renderer split. Browser UI is another
consumer of the same model, not another decoder.

### Proposed screen tabs

- **DISCOVER:** active frequency, supported bands, dwell progress, signal events, known
  decodes, unknowns; start/pause/stop actions.
- **DEVICES:** deduplicated sensor cards showing protocol, frequency, reported ID, class,
  last-seen age, RSSI (only if measured), and evidence confidence.
- **DETAIL:** latest *verified or explicitly tentative* measurements, units, history,
  packet count, data age, and a clear label if a checksum or identity is unavailable.
- **UNKNOWN:** unclassified signal captures, waveform hints, frequency, timestamps, capped
  sample evidence and export for offline reproducible analysis.
- **SETUP:** region-independent custom presets, user-selected bands, receive-only mode,
  scan dwell, capture limits, unsupported hardware notices and archive settings.

## Existing reusable APIs (confirmed from main at cdc8181)

- `apps/orcsdr-tab5/ui/radio_session.*`: session ownership, tuner generation and state
- `apps/orcsdr-tab5/ui/scan_engine.*`: bounded list/range stepping and retune callbacks
- `apps/orcsdr-tab5/ui/lora_preamble_detector.*`: specialist LoRa preamble research
- `apps/orcsdr-tab5/ui/dashboard_registry.*`, `screen_controller.*`: navigation
- `apps/orcsdr-tab5/ui/acars_*.*`: example decoupled decoder/model/rendering pattern
- `apps/orcsdr-tab5/ui/AGENTS.md`: rendering and regression requirements

Do NOT add an unrelated tuner-owning scanner or a second shared radio implementation.
Use the existing scan engine with a receive window per channel. Its current
settle_ms is uniform across a Plan; honor actual dwell requirements using
sub-plans or a reviewed extension, not by silently ignoring per-target dwell.

## This branch's implemented files

- `iot_sensor_discovery.hpp/.cpp`: fixed-capacity 32-sensor inventory with
  trustworthy *protocol + protocol-provided device ID + tuned center* identity;
  deduplication, evidence counters, latest readings, TTL detection, bounded
  least-recently-seen replacement and an explicit last-packet integrity bit.
- `iot_sensor_scan_profiles.hpp`: initial US and EU candidate *centers* and modulation
  hints. These are not exhaustive, not a frequency authorization, not device support,
  and not a tuner configuration. Do not report coverage based only on their presence.
- `apps/orcsdr-tab5/tests/iot_sensor_discovery_test.cpp`: host-only invariants.
- `.github/workflows/iot-sensor-host-tests.yml`: C++17 host CI.
- `apps/orcsdr-tab5/main/CMakeLists.txt`: model source compiled, but not yet linked
  to an IQ stream or navigation entry.

No decoder has been implemented in this branch. A `DecodedEvent` is a contract for
future decoders, not proof of a received sensor.

## Follow-on implementation stages

1. **RF ingress:** lease receiver session, capability-filter targets, configurable dwell,
   cyclic scan, abort/restore on failures, per-band reception and dropped-IQ stats.
   Share the actual IQ receive callbacks rather than starting a second SDR owner.
2. **Native decoders:** implement or properly license original OOK/ASK and FSK/GFSK
   demodulators with pulse timing; add a small set of genuine known device protocols,
   each with documented framing, checksum behavior and off-air IQ regression files.
   Metadata/protocol catalog alone is NOT a decoder.
3. **Model delivery:** bounded SPSC decode-event queue; call `Inventory::ingest` only
   on its owning task; no heap allocations in the high-rate callback. Extend
   `DecodedEvent` with tuner/radio generation to reject stale post-retune events.
4. **UI:** renderer, controller, navigation registration, telemetry surfaces, unknown
   capture controls; exercise touch/layout and no-full-screen-redraw tests.
5. **Archive / cross-platform:** SD-backed portable JSONL/CBOR, local web consumer,
   replay harness, and optional separately deployed `rtl_433` JSON adapter.
6. **Hardware gate:** Tab5 + RTL-SDR V4 and E4000 on supported frequencies; repeatable
   known-sensor trials, negative false-positive cases, long-dwell captures, no
   unacceptable IQ drops, peak memory/stack tests, sleep/wake and hot-swap behavior.

### Important boundaries

- Default scan centers cannot guarantee discovery of a device that transmits once
  every five minutes, hops across many channels, or uses a proprietary PHY.
- **Frequency is not sensor identity.** An RF burst with no validated decoder is
  `unclassified`; a protocol with no stable ID is `unattributed`.
- Protocol checksums establish transmission integrity, **not** cryptographic source
  authenticity. Do not imply that CRC-verified wireless readings are tamperproof.
- No claims to decode encrypted LoRaWAN, WirelessHART, Zigbee, BLE or proprietary
  formats without relevant lawful keys/authorized integration and a working PHY.
- The RTL-SDR receiver range does not include 2.4 GHz; an ESP32-C6 or alternate
  hardware needs separate integration for BLE/Zigbee/Wi-Fi.
- `operating_hours` must be received from a device or derived from proven
  uptime/run-state coverage. Gaps in scanning prevent accurate inferred hours.
- Sensor IDs are treated as untrusted input. Sanitize names and limit persistent
  logs; no automatic cloud export or unrestricted LAN endpoint.
- The model's 32-bit uptime age arithmetic assumes elapsed intervals under 2^31 ms;
  schedule a reset/epoch strategy before multi-week live deployments.

### License and upstream

`merbanan/rtl_433` (https://github.com/merbanan/rtl_433) is a valuable
**independent reference / optional external process**, supporting many OOK/FSK
decoders and JSON/HTTP/MQTT output. Its GNU GPL-2.0 license is not a drop-in
match for OrcSDR's AGPL-3.0-only/commercial dual license. Do not paste its
decoder sources into OrcSDR. Evaluate source licensing and standalone adapter
boundaries with counsel for distributable appliances. A separate-process
JSON adapter does not by itself grant rights to embed the decoder.

### Host test

From repository root (on a workstation with `g++`):

```sh
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I apps/orcsdr-tab5/ui \
  apps/orcsdr-tab5/ui/iot_sensor_discovery.cpp \
  apps/orcsdr-tab5/tests/iot_sensor_discovery_test.cpp \
  -o build/orcsdr-iot-sensor-test
./build/orcsdr-iot-sensor-test
```

Expected: `IoT sensor discovery host tests PASS`.
No physical RF validation can occur without a receiver and sensor test captures.
