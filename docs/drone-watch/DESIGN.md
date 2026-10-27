# Drone Watch — product and engineering design

**Date:** 2026-10-26  
**State:** Proposed; all behavior below is a design requirement unless explicitly called out as existing OrcSDR functionality.  
**Evidence:** [private REA research PR #3](https://github.com/hardcoreerik/REA-Results/pull/3). Source references there are pinned to exact GitHub commits. Do not treat external manufacturer claims as independently reproduced.

## 1. Objective and product boundaries

Deliver approachable, receive-only drone-awareness on ESP32-P4 products with:
1. **Broadcast Remote ID** observation and decode (standard messages received through supported BLE and/or Wi-Fi paths);
2. clear presentation of radio source health, freshness, decoded claims, and optional map;
3. optional, separately verified **RF activity**, **analog FPV receiver/video**, and **acoustic** experiments.

This is *not* a military counter-UAS/anti-drone appliance, a universal drone detector, a radio direction finder, a flight-control tool, or a video decryption product. It never transmits, jams, interferes, follows pilot commands, or presents a threat score. Do not silently redirect, reconfigure, or replace working radio and Wi-Fi firmware.

**Primary users:** hobbyist observer, RF learner, radio monitoring enthusiast, maker experimenting with compatible receivers. Main activity: open dashboard -> see receiver availability -> observe decoded public broadcasts -> inspect source/age -> optionally review a reported position.

## 2. Trust and classification: evidence must remain explicit

**Never display 'DRONE CONFIRMED' from raw RF or acoustic energy.** Even valid Remote ID is a *received broadcast claiming aircraft identity and position*, not independent cryptographic proof of who/where. Keep reported aircraft position separate from the observer's own GPS location. No 'distance' without known observer position, a valid claimed aircraft position and a plainly labelled calculation; RSSI never converts directly into distance.

Proposed event kinds:

| Kind | Admissible input | User-facing wording | Must NOT imply |
| --- | --- | --- | --- |
| `remote_id_message` | successfully parsed supported OpenDroneID message from logged BLE/Wi-Fi acquisition | `REMOTE ID RECEIVED` | verified ownership, true location, malicious intent |
| `rf_activity` | SDR power/occupancy observation with frequency, span, time and calibration caveats | `RF ACTIVITY` | UAV identity, pilot location, video |
| `analog_video_signal` | independent compatible analog receiver signal/lock output | `VIDEO SIGNAL` | a particular drone identity or working video frame |
| `video_frame` | a successfully digitized permitted receiver frame | `VIDEO AVAILABLE` | all drones are watchable or digital encryption can be bypassed |
| `acoustic_candidate` | microphone features/classification with test provenance | `POSSIBLE ROTOR SOUND` | aircraft identification, location, validated classifier accuracy |
| `system_state` | adapter/lifecycle transitions | `SCANNER PAUSED`, `SOURCE UNAVAILABLE`, etc. | that no drones exist when not scanning |

Consider **no Remote ID observed** distinct from **no drone nearby**; silent aircraft, out-of-range, encrypted/proprietary links, incompatible PHYs and unavailable sensors are not observable by the current configuration.

### Ingest and normalization contract (proposed, not implemented)

```text
source_id                 stable local source identity, not a claimed aircraft ID
receiver_kind             hosted_c6_ble | hosted_c6_wifi | orcnode | rtl_sdr | mic | analog_video
transport                 ble4 | ble5_extended | ble5_coded | wifi_beacon | wifi_nan | iq | audio | video
monotonic_rx_ms            local monotonic time used for ordering/staleness
utc_rx_ms?                 optional valid wall-clock capture time; do not synthesize it
raw_sample_ref?            optional opt-in capture reference (not mandatory raw payload persistence)
rssi_dbm?                  only when supplied by the actual receiving API; do not fabricate
receiver_channel?         channel/frequency when captured, not guessed from aircraft type
parse_status              ok | malformed | unsupported_version | incomplete | not_applicable
message_type?             basic_id | location | system | self_id | operator_id | auth | pack | ...
reported_aircraft_id?     claimed ID; may be missing, rotate, or be spoofed
reported_position?        optional validated paired lat/lon (transmitter claim)
reported_altitude?        optional value + unit/reference/validity; never invent a datum
auth_state?               unknown | present_unverified | verified [only with real verified crypto]
source_health             running | paused | degraded | unavailable | error
confidence_basis          typed evidence description, not a numerical threat/confidence percentage
provenance                decoder version + source firmware/capture metadata where feasible
```

Reject NaN/out-of-range coordinates; distinguish `unknown` from real (0,0); never re-use last valid coordinates after a later invalid/absent position update without displaying age/state. Preserve arrival ordering; cap string lengths, buffers, records, queues and disk writes. Do not merge unrelated transmitters based on equal RSSI, frequency or rotating MAC. Make track TTL configurable and evidence-derived; a **15-second synthetic test value is not a product requirement**. Missing clock means relative time only.

### Source-state machine

`UNAVAILABLE -> READY -> SCANNING -> PAUSED -> SCANNING` with `DEGRADED` and `ERROR` transitions. Log why paused: foreground Wi-Fi operation, C6 Hosted disconnect, RF tune ownership, video receiver tuned away, device backgrounding, operator selection, data source lost. Paused windows contribute **no coverage**. Each source declares supported methods; absent support is `UNSUPPORTED`, not a green idle state. Never display `SCANNING` until data actually reaches the acquisition callback (or the adapter has proven an active receive path).

## 3. System boundaries and ownership

```text
C6 BLE4 / BLE5 / Wi-Fi RX (conditionally supported)   OrcNode/Android optional passive source
             |                                                |
       acquisition adapters -------------------+---------------+
                                               |
                    validated, bounded packet envelope
                                               |
                  OpenDroneID core C adapter + host-tested fixtures
                                               |
                 source-aware event aggregator + stale eviction
                          |                    |
                       UI model           optional SD journal
                          |
                Drone Watch Tab5 renderer
                          |
                   OrcMaps report layer

RTL-SDR -> frequency/occupancy event only -------------^
Optional analog RX/digitizer -> signal/frame status ---^
Optional microphone -> acoustic candidate ------------^
```

- **`apps/orcsdr-tab5/ui/drone_watch_model.{hpp,cpp}` (proposed):** platform-neutral bounded observation/track store, staleness, source capabilities, source and event status, host tests. No M5GFX, file I/O, C6 headers, radio-driver calls.
- **`components/drone_remote_id/` (proposed):** license-reviewed parser wrapper around official OpenDroneID C core, with exact pin, attribution, corrupted-packet tests. Determine whether vendoring or a separate component is allowed before choosing implementation.
- **`apps/orcsdr-tab5/ui/drone_watch_sources.{hpp,cpp}` (proposed):** capture adapters, C6 Hosted capability probing, queue and lifecycle control. No framework-specific graphics, no cross-task raw pointers.
- **`apps/orcsdr-tab5/ui/drone_watch_dashboard.{hpp,cpp}` (proposed):** snapshot rendering and touch contract; one framebuffer owner via existing `screens::Id`; shared header control path. `main.cpp` only register/open/close/tick and route snapshots/actions.
- **`apps/orcsdr-tab5/ui/drone_watch_app.{hpp,cpp}` (proposed):** lifecycle composition, import/replay/test injection and SourceStatus. Pattern after `dead_air_app`, without sharing its supernatural event semantics.
- **`apps/orcsdr-tab5/ui/offline_map.*` / shared OrcMaps layer:** reuse existing map service and geographic labels preference; no second hand-drawn world map.
- **`esp-rtl-sdr`:** unchanged dependency and immutable pin unless a truly receiver-generic fix is established in its own repository. This dashboard is **application-level**.

Use exactly one RTL-SDR owner and one framebuffer owner. No DSP or serial/USB callbacks draw UI or perform SD writes. Use fixed-size messages, bounded SPSC queues or an existing safe analogous primitive, and a low-priority journal worker only if logging is explicitly enabled. API versioning must permit future OrcNode adapters.

## 4. Hardware reality matrix

| Hardware | Designed capability | Unknown or constraint |
| --- | --- | --- |
| M5Stack Tab5 ESP32-P4 + onboard ESP32-C6 | Potential BLE/Wi-Fi Remote ID collector | Existing ESP-Hosted is used for Wi-Fi over SDIO. BLE HCI, Wi-Fi beacon metadata, NAN and concurrent operation require separate proof on *the shipping firmware*; C6 capability on paper is not a success. |
| RTL-SDR Blog V4 / supported RTL dongle | narrowband measurements and optional sub-GHz RF activity | Cannot directly receive 2.4/5.8 GHz; limited instantaneous bandwidth; raw RF peaks are not drone classification; existing radio must not be broken by scan loops. |
| ESP32-C6/S3 OrcNode | optional independent Remote ID observation feed | Network/API schema and hardware must be proven; no passive capture or discovery promises before tests. |
| RX5808-type or other analog video receiver | optional compatible analog FPV signal and composite output | Requires an actual digitizer/interface and separate PAL/NTSC format/frame acceptance; RSSI or sync ≠ decoded image. |
| Microphone (Tab5) | optional sound level/features | Acoustic classification and detection range require negative and positive recordings, environmental controls and hardware acceptance; do not assume direction finding. |
| P4 display variants (720×720 etc.) | adaptive layout via OrcUI work | Do not ship a second manually forked dashboard; capability-state renderer must respond to dimensions and input geometry. |

**Interference and concurrency:** `docs/user-guide/wifi-tab5.md` currently documents pausing SDR during Wi-Fi scan/connect/catalog. Do not quietly remove this guard for Drone Watch. Test C6/Wi-Fi coexistence and SDIO/heap impact independently, then either explicitly pause another activity or use a companion OrcNode. Any C6 firmware update is a separately approved operation, with M5 Launcher and factory firmware compatibility tested.

## 5. User experience / information architecture

**Glanceable default:** one prominent reception-source state, latest validated Remote ID message, a small activity/history strip and two clear actions: `Details` and `Sources`. Do not use a cramped six-panel cockpit on the 5-inch Tab5.

Suggested user-facing pages (only working tabs are enabled; unsupported ones have an explanation, never placeholder live metrics):

1. **WATCH (v1):** `SCANNING BLE` / `SCANNER PAUSED` / `NO RID OBSERVED` plus last observation, age, monitored methods and source errors. Never say `AIRSPACE CLEAR`.
2. **AIRCRAFT (v1):** bounded list, each row `Remote ID received`, claimed identifier when present, last seen, valid/invalid fields. Tap for decoded message details and provenance. Avoid suggesting every transmitter = distinct aircraft.
3. **MAP (v1 after OrcMaps gate):** show only valid reported positions with `BROADCAST LOCATION — UNVERIFIED`, visible age, stale/expired behavior, one map provider and geographic label preference. No inferred location from receiver RSSI; operator-location fields require deliberate privacy UX.
4. **SOURCES (v1):** BLE4/extended/Beacon/NAN support matrix, last packet, busy/paused/error reason, C6 firmware and power/coexistence health (without exposing credentials).
5. **RF / VIDEO / AUDIO (later experimental pages):** add only after respective hardware gates. Expose receiver frequency/lock state, scanning pause and actual decoded-frame state; advanced graphs one tap away.

**1280×720 design:** preserve canonical dashboard header/top controls and accessible touch targets; large typography and short rows. **720×720 square:** reflow cards into vertical stack or paginated detail; no screen-specific cloned logic. Keep display pages usable when there is **zero** received traffic or when a source is disconnected. Empty states must differentiate inactive source from active scan with no packets.

## 6. Privacy, safety, security and traceability

- Receive-only, passive monitoring of publicly broadcast Remote ID and the user's own/authorized RF/video test sources.
- Treat all IDs, coordinates, display strings, and inbound source packets as untrusted; no remote URL execution or source text injection.
- No automatic persistent log of operator/home location or full telemetry. Local optional session history with retention/clear controls; redact unnecessary identity fields in shared screenshots/exports.
- Do not infer or reconstruct operator whereabouts from RF signatures. No tactical prediction, interception, control injection or signal disruption.
- Test with owned/authorized beacons/video test transmitters. Label simulator/replay state **TEST DATA** on every affected view and in logs. Keep development-only packet injection behind test builds/authenticated diagnostics.
- Every feature carries an evidence level from `PROJECT_STATUS.md`: Planned, Implemented, Build-Verified, Runtime-Verified, Hardware-Verified, RF-Verified, Regression-Tested and Experimental. The status follows tests, not intent.

## 7. Non-goals and dependencies

No DJI proprietary-link decoding claim, no decryption of video, no universal 5.8 GHz SDR from the C6, no jammer, no RF-based distance/direction estimate, no attack logic, no automatic location-sharing service, and no requirement to buy another radio just to use the basic app. Detailed phasing: [PHASING.md](PHASING.md). Qualification matrix: [VALIDATION.md](VALIDATION.md).
