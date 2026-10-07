# OrcControl v4 encrypted payload

This table describes the 64-byte CONTROL PAYLOAD inside an authenticated
version-4 session, not a raw ESP-NOW frame. Integers here are little endian.
CRC-32/IEEE covers bytes 0–59; it detects codec corruption and is not security.
Raw version-3/ORDL frames are rejected. [Secure pairing](docs/SECURE_PAIRING.md)
describes the outer fragments, trust exchange and AES-GCM session envelope.


| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic `ORDL` |
| 4 | 1 | version, currently 4 |
| 5 | 1 | message type |
| 6 | 1 | role: Dial 1, receiver 2 |
| 7 | 1 | band: the Tab5's band profile id plus 1 for the frequency on screen, 0 when unknown (older Tab5 firmware sends 0; the Dial then shows RADIO) |
| 8 | 4 | sender boot-session ID (inside authenticated session) |
| 12 | 4 | sequence |
| 16 | 4 | acknowledged sequence |
| 20 | 4 | signed command value |
| 24 | 4 | actual/requested frequency in integer Hz |
| 28 | 4 | tuning step in integer Hz |
| 32 | 2 | gain in 0.1 dB units |
| 34 | 2 | squelch setting |
| 36 | 2 | signal in dBm |
| 38 | 1 | mode |
| 39 | 1 | volume |
| 40 | 1 | flags: bit 0 means signal is measured and valid |
| 41 | 1 | authoritative dashboard ID in `RADIO_STATE` |
| 42 | 1 | semantic action ID (command) |
| 43 | 1 | dashboard view ID |
| 44 | 4 | authoritative state revision |
| 48 | 4 | selected item or parameter (dashboard-specific) |
| 52 | 4 | available item count (dashboard-specific) |
| 56 | 4 | capability flags (dashboard-specific) |
| 60 | 4 | CRC-32 |

Types: 1 HELLO, 2 PAIR_REQUEST, 3 PAIR_ACK, 4 HEARTBEAT, 5 TUNE_RELATIVE, 6 TUNE_ABSOLUTE, 7 SET_STEP, 8 SET_MODE, 9 SET_GAIN, 10 SET_VOLUME, 11 SET_SQUELCH, 12 REQUEST_STATE, 13 RADIO_STATE, 14 ERROR, 15 SET_DASHBOARD, 16 SEMANTIC_ACTION. `SET_DASHBOARD.value` is a dashboard ID. `RADIO_STATE.dashboard` is the receiver's current screen, including changes made on the Tab5. IDs follow the current Tab5 registry: 0 Home, 1 FM, 2 P25, 3 ADS-B, 4 Shortwave, 5 Weather, 6 CB, 7 LoRa, 8 Airband, 9 Marine, 10 Satellite, 12 Settings, 13 RF Lab, 14 Wi-Fi Analysis, 15 POCSAG, 16 AM. ID 11 Utilities is a menu and is rejected. `SEMANTIC_ACTION.action` is the `ActionKind` in `src/controller.hpp`; `value` is a signed relative delta (Hz for `tune` on tunable dashboards, a step count on Home). The receiver must verify the active dashboard and reject unsupported actions with `ERROR`, never silently reinterpret them as tuning. `RADIO_STATE.ack` identifies the command applied; resends use the original sequence. The test receiver rejects duplicate sequence numbers and resends the same authoritative state. Frequency is clamped to 24 kHz–1.766 GHz in the test receiver, matching the Tab5's documented wide tuner bounds; per-mode limits remain a Tab5 integration decision.

**Home (dashboard 0) is a full-range VFO.** `SEMANTIC_ACTION` `tune` carries a signed count of steps, not Hz; the Tab5 applies
the step and channel raster of the band it is in and moves to the next band's raster when a spin crosses a band edge.
`step` cycles the band's step list, `span` (`value > 0` zooms in) changes the spectrum span, and `filter` (`value > 0`
widens) moves through the mode's usual widths; `span` and `filter` are `ActionKind` values 21 and 22, appended after
`activate`. `SET_MODE` on Home takes 1 NFM, 2 AM, 3 WFM, 4 USB, 5 LSB. `TUNE_ABSOLUTE.value` is Hz: on Home any frequency
from 24 MHz to 1.766 GHz, and on FM, AM and the other tunable dashboards a frequency inside that dashboard's band
(the Tab5 validates again and answers `ERROR` otherwise). In Home's `RADIO_STATE`, `mode` is the demodulation in use
(1 to 5), `step_hz` the band's step, `selected` the span in Hz and `item_count` the filter width in Hz.

The compact state fields at offsets 43–59 have meanings only within the named dashboard. They are a temporary common envelope; production dashboard-specific payloads and capability bits must be specified alongside the Tab5 bridge before those fields can represent aircraft, nodes, messages, presets, or settings. The stand-alone test receiver supplies no such lists and returns `ERROR` for unsupported selection actions.

The Dial sends one command at a time and retries twice at 650 ms using the
same application sequence inside fresh encrypted messages. Session liveness
uses authenticated traffic and a five-second timeout, followed by a bounded
reconnect attempt. Disconnect retains trust, clears session keys and pauses
retries for the current boot. A cached encrypted final notice is retried twice
without retaining keys. Only an explicitly requested, freshly authenticated
Connect can override a peer's manual stop.

The older receiver stub under `examples` is historical prototype code and is
not a version-4 secure receiver. Do not use its raw packet handling as integration
guidance. The production receiver is the OrcSDR Tab5 secure worker/bridge.
