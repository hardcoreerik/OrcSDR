# Drone Watch — shared data, lifecycle and provenance contracts

**Status:** design proposal. Validate against actual published OpenDroneID reference implementation before assigning packet field names or decoder values. Never assume the current ESP-Hosted/C6 firmware offers any specific monitor/HCI interface.

## Data sources and capability declaration

Each adapter reports:
`source_id`, `receiver_kind` (`hosted_c6_ble`, `hosted_c6_wifi`, `orcnode`, `rtl_sdr`, `analog_video`, `microphone`, `fixture_replay`),
`firmware_version?`, `acquisition_mode`, supported `transport` list (BLE4/BLE5 extended/BLE5 coded/802.11 beacon/NAN/IQ/analog video/audio),
`state` (`unsupported`, `unavailable`, `ready`, `scanning`, `paused`, `degraded`, `error`),
`pause_reason?`, measured `last_packet_monotonic_ms?`, packet/drop/error counters, and observation intervals/gaps (for coverage views).

A hardware chip and driver support table are **not** equivalent to a successful receive state. One source may support BLE4 but not NAN. `capability=unknown` must be distinct from `supported` and `unsupported`.

## Canonical observation

```text
observation_key         stable local key for dedupe and UI navigation
source_id               local receiver name, not airplane identifier
receiver_kind           sensor origin
transport               actual physical acquisition method
rx_monotonic_ms          primary time order; valid even without UTC
rx_utc_ms?              only when verified wall-clock quality
clock_quality           unknown | approximate | synchronized
center_hz? / span_hz?   actual tuner/acquisition footprint, not advertised coverage
rssi_dbm?               only authentic radio-reported RSSI with signed dBm
iq_power_dbfs?          separate baseband relative power field
payload_size?           bounded packet bytes received
packet_family           remote_id | rf_activity | analog_video | acoustic | health
decoder_version?        software revision and protocol version
parse_status            ok | unsupported | malformed | incomplete | not_applicable
message_kind?           BasicID / Location / System / SelfID / OperatorID / Auth / Pack etc.
reported_aircraft_id?   untrusted broadcast claim
reported_position?      paired validated latitude+longitude and validity/status
reported_altitude?      signed value with unit + actual reference/validity
reported_speed?         value and unit only if decoded and valid
reported_heading?       valid azimuth bearing if decoded
auth_state              unknown | present_unverified | verified
capture_ref?            opt-in persisted capture/clip reference, not required for live UI
provenance_note         e.g., TEST DATA, external OrcNode source, retained replay, receiver type
```

A **track/aircraft view** is a time-bounded grouping of suitable decoded broadcast claims, not an authoritative aircraft registry. Do not merge independent sources based on RSSI, band, apparent position or MAC alone; any grouping policy must be host-tested and explicitly documented.

## Validity and safe display

- No numeric sentinel is reused as a meaningful valid zero. Valid (0,0) must remain possible if the format permits; unknown/invalid pair never plotted.
- Treat incoming strings as untrusted UTF-8/bytes; bounded text, escaping where needed, no format-string injection, fixed-size buffers and explicit truncation marker.
- `reported` position and `observer` position are separate fields and require explicit provenance and privacy rules.
- Broadcast operator/system/auth message presence does **not** imply cryptographic trust. Never show `verified` unless independent signature validation actually ran and passed.
- Clock unavailable: show `LAST SEEN 4 s AGO`; UTC time `-- (clock unavailable)`; no invented wall-clock.
- Out-of-order packets cannot regress current-state fields; preserve history as arrived when relevant. Expiry and last-seen windows configurable after evidence; **do not import arbitrary synthetic 15s TTL as standard**.
- `NO RID OBSERVED` only while a known supported source is actively scanning. During failure/pause use `NOT MONITORING`, explaining the reason.

## Derived features with strict semantics

| Field | Valid derivation | Never derive |
| --- | --- | --- |
| Latest event | timestamped actual adapter event | phantom drone from idle RF baseline |
| Receiver coverage | intersection of supported modes and measured active listening intervals, excluding retunes/pauses/errors | continuous coverage from radio powered-on indicator |
| Alert | user-approved deterministic predicate on validated event/state | threat/danger percentage or military interpretation |
| RF occupancy | measured sampled frequency bins over observed dwell windows, include gaps | generalized 2.4/5.8 GHz drone sweep from RTL-SDR V4 |
| Aircraft location map | reported valid aircraft lat/lon with time/source | RF bearing/distance/true owner location from signal level |
| Observed track | qualified sequence of broadcast locations with ages and gaps | completed flight route between missing positions |
| ADS-B context | independently obtained ADS-B snapshot, its own source/ID and caveat | proof that a Remote ID emitter is an ADS-B aircraft |
| Analog video | confirmed compatible receiver + digitized displayed video frame | visual output from a radio RSSI peak |
| Acoustic monitor | opt-in microphone level or separately validated candidate model | independent aircraft ID/location or proven drone class from noise |
| Signal comparison | physically measured coincident time windows with separate device labels | assumed synchronized RF/Remote ID/video capture |

## Persistent data and media

RAM default, SD **opt-in** for records, clips, observations and session journals; use a low-priority worker and bounded queue following existing Dead Air patterns while auditing the known shared SD access and unmount constraints. Journals versioned JSONL with unit/source/validity, sequencing, manifest hashes, incomplete-write recovery and explicit loss counts. Raw video/audio test recordings are opt-in and never capture silently. Redact unnecessary operator-home coordinates by default.

## Replay/test data

A replay source is permanently identified as `fixture_replay`; all cards/maps/logs/pins derived from it carry `TEST DATA`. A replay may seek/step and pause; replay time and actual device clock are separate. Never allow a simulator switch to make live acquisition appear successful, including through a combined counts badge. Recording imported from actual receiver still needs source provenance and replay banner.

## API shape proposals

`SourceAdapter::capabilities()`, `SourceAdapter::snapshot()`, `SourceAdapter::start()/stop()`; a bounded event queue and immutable per-tick `DroneWatchSnapshot`. Decoder core owns parsing, app owns acquisition/session/snapshot, page renderer reads snapshot. Long operations are owned by workers and callback-completion events. Trace image/source revisions for audit.

## Gate dependencies

- OpenDroneID decode and bounded model: P1.
- Onboard ESP32-C6 acquisition capability: P2 separate physical gate; fallback supported if blocked.
- WATCH/AIRCRAFT/SOURCES replay UI: P3.
- Verified live Remote ID and OrcMaps claims: P4.
- Optional journal/OrcNode: P5; RF analysis P6; analog video P7; acoustic P8.
- Final release behavior: P9.
