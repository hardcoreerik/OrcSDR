# Weather Dashboard Design

## Goal

Replace OrcSDR's current generic NOAA Weather Radio route with a first-class,
offline-first Weather dashboard for the M5Stack Tab5. The dashboard must be
useful with Wi-Fi disabled, must distinguish direct RF observations from cached
or optional network data, and must never imply that one RTL-SDR is monitoring
multiple bands at the same time.

The product surface is five fixed tabs at 1280x720: **NOW**, **FORECAST**,
**MAP**, **RF WEATHER**, and **REPORTS**. Transient acquisition experiences such
as **Weather Hunter**, **Balloon Hunter**, and later **Satellite Hunter** are
modal/full-screen submodes rather than permanent tabs.

## Product principles

1. **Offline first.** Direct RF, local state, locally stored catalogs/maps, and
   SD history are primary. Network weather is optional enrichment and ships
   disabled by default.
2. **Source honesty.** Every displayed observation has a source class and age.
   A cached RF observation is not drawn as live; an Internet alert is not
   labeled as RF; a frequency hit is not called a decoded station.
3. **One tuner means one foreground RF job.** Weather does not pretend NOAA,
   personal weather sensors, radiosondes, and satellites are being sampled
   simultaneously.
4. **Background work never steals the receiver.** Opportunistic Weather jobs may
   run only while the shared receiver is idle. Foreground listening/tracking
   always wins.
5. **Reuse OrcSDR services.** Weather consumes the existing receiver/DSP path,
   `radio_session`, shared location, signed data catalog, `offline_map`,
   `time_service`, SD wrapper, navigation/screen ownership, OrcDial bridge, and
   standard dashboard chrome. It does not create parallel equivalents.
6. **International-ready architecture.** NOAA/NWS behavior is a United States
   provider/profile, not the definition of the Weather dashboard. Regional RF
   channel plans and optional online providers must be replaceable data or
   provider modules.

## Current baseline

At the design baseline (`main` commit
`8b0a7132a387abfa9fc1be3d1eaf0d7be271eed3`), Weather is already present in the
dashboard registry and OrcDial wire IDs, but opening it routes to the shared
`RtlBand::wx` NFM receiver surface at the default NOAA frequency. The existing
NFM/WX DSP and audio path are retained. This project changes the Weather product
surface and orchestration, not the proven NFM demodulator.

Existing reusable pieces include:

- `apps/orcsdr-tab5/ui/radio_session.*` for receiver ownership/state.
- `apps/orcsdr-tab5/ui/offline_map.*` for SD-backed local map rendering.
- `apps/orcsdr-tab5/ui/receiver_location.*` / Settings receiver location as the
  canonical location shared with ADS-B, Airband, and OrcMaps.
- `apps/orcsdr-tab5/ui/catalog_sync.*` and the existing signed
  `noaa_weather` catalog slot.
- `apps/orcsdr-tab5/ui/orcsdr_storage.*` for SD access.
- `apps/orcsdr-tab5/ui/rf_lab.cpp` as the precedent for CSV + JSON + screen
  image + SHA-256 session bundles.
- `apps/orcsdr-tab5/ui/time_service.*` for UTC validity and timestamps.
- Current M5GFX dashboard conventions: black background, dark panel fill,
  cyan border/accent, status green/yellow/red, shared header controls, and
  fixed bottom navigation.

## Scope decomposition

The design is implemented in three independently reviewable plans:

1. **Weather Dashboard Foundation** — dedicated screen/runtime, five-tab UI,
   offline data/source model, NOAA listen/scan primitives, local map/catalog
   integration, SD history/report bundles, and optional-network policy plumbing.
2. **Weather Hunter** — RF job arbitration, on-demand multi-band discovery,
   NOAA SAME decoding, initial personal weather-sensor decoders, and radiosonde
   discovery/tracking.
3. **Satellite Weather** — offline pass prediction from cached orbital data,
   event-driven 137 MHz acquisition, LRPT receive/decode experiments, and
   imagery/report integration.

The first plan produces a usable dedicated Weather dashboard without requiring
Weather Hunter or satellite decoding to succeed.

## Non-goals

- No continuous wideband monitoring across NOAA, ISM sensors, radiosondes, and
  satellites with one RTL-SDR.
- No automatic network request merely because Weather was opened.
- No cloud account, telemetry upload, or dependency on a companion phone.
- No speech-to-text requirement for NOAA Weather Radio in the first phase.
- No claim that a nearby RF sensor belongs to a named person or address.
- No averaging of unrelated nearby sensors into an authoritative local weather
  value unless the UI identifies it as a derived consensus and shows its input
  count/spread.
- No raw IQ recording by default; raw IQ is an explicit diagnostic capture.
- No copy/paste of third-party decoder implementations. Protocol support must be
  independently authored from public specifications and/or captured fixtures,
  with provenance recorded.
- No satellite feature built around retired NOAA POES APT service.

## Screen and visual contract

Weather follows the established 1280x720 OrcSDR dashboard language rather than
an Internet weather-app skin.

- Header height: **132 px** where the shared audio-dashboard header is used.
- Footer tabs start at **y=630**, height **90 px**.
- Exactly five footer tabs, **256 px each**.
- Base colors follow current dashboards: `TFT_BLACK`, panel `0x0841`, cyan
  `0x2e7f`, green `0x6fe8`, muted `0x8c71`, grid `0x2945`, with yellow/red only
  for caution/alert state.
- Shared Home, battery, mute/audio and Settings affordances use existing
  `dashboard_audio_control` behavior where applicable.
- Touch targets remain finger-sized and must not overlap at 1280x720.
- Dynamic regions repaint independently; the implementation must not introduce
  periodic full-screen redraw flicker.

### Tab 1 — NOW

NOW is an observation surface, not a promise that a forecast service exists.
It prioritizes current direct/local information:

- most recent temperature/humidity/pressure/wind/rain values, if present;
- source badge (`RF`, `LOCAL`, `CACHE`, `ONLINE`) and age for each observation;
- pressure/trend and other locally derived values only when the inputs exist;
- active alert summary split by direct RF vs optional network source;
- compact RF status summary showing last NOAA/sensor/sonde sample times.

If a value is unavailable, draw `NO CURRENT MEASUREMENT` or equivalent. Never
replace absence with zero.

### Tab 2 — FORECAST

FORECAST remains useful offline without pretending OrcSDR can derive a full NWS
forecast from local thermometers.

Offline/default content:

- locally calculated trend/outlook cards from available observations;
- cached forecast snapshot, clearly timestamped, if one was previously saved;
- direct shortcut to `LISTEN TO WEATHER RADIO` where a regional voice service
  profile exists;
- active locally decoded alert metadata when available.

Optional online provider data may populate richer hourly/daily cards only when
network enrichment is enabled or manually requested. Its cards are marked
`ONLINE` and retain their fetch time after caching.

### Tab 3 — MAP

MAP uses the existing `offline_map` and canonical receiver location.

Offline-capable layers include:

- receiver position;
- locally installed weather-radio transmitter catalog entries;
- decoded radiosonde tracks/positions;
- saved report markers;
- later satellite ground tracks from cached orbital data.

Radar, lightning, and other network imagery are optional layers only. When no
map pack is installed, the tab remains functional with a clear installation
message and list-based RF/location information.

### Tab 4 — RF WEATHER

RF WEATHER is the acquisition control surface. It does not claim simultaneous
monitoring.

Primary cards:

- **Weather Radio** — strongest/last-known channel, signal state, `LISTEN`, and
  `SCAN CHANNELS`.
- **Local Weather Sensors** — discovered device count by band and `SCAN`.
- **Radiosondes** — last discovery state and `SCAN 400–406 MHz`.
- **Weather Hunter** — user-triggered sequential discovery across enabled jobs.
- **Next Satellite Opportunity** — only when the satellite plan has valid local
  orbital data.
- **Background Policy** — `OFF`, `IDLE ONLY`, or `AGGRESSIVE`; default is
  `IDLE ONLY`, but background jobs still require an idle receiver.

Specialized full-screen modes are entered only after an explicit action:

- `Weather Hunter` for sequential discovery;
- `Balloon Hunter` after a radiosonde candidate/decoder lock;
- `Satellite Hunter` near a valid predicted pass.

### Tab 5 — REPORTS

REPORTS combines lightweight on-device history with complete SD-backed report
bundles. Users can open recent sessions without a PC and can remove/export
complete bundles from SD.

Report types include:

- snapshot;
- field session;
- Weather Hunt;
- NOAA/SAME event;
- weather-sensor discovery;
- Balloon Hunt;
- satellite pass.

## Source model and freshness

Every observation carries enough metadata to keep the UI truthful:

```cpp
enum class SourceKind : uint8_t {
  direct_rf,
  local_sensor,
  local_derived,
  cache,
  online,
};

enum class Freshness : uint8_t { live, recent, stale, expired, unavailable };

struct ObservationMeta {
  SourceKind source;
  uint32_t observed_utc;
  uint32_t age_seconds;
  Freshness freshness;
  uint8_t input_count;
};
```

Exact thresholds are field-specific and pure-model tested. A timestamp is UTC
when `time_service` has a valid wall clock; otherwise the record stores uptime
and is labeled as relative-time evidence until a valid UTC mapping exists.

A derived consensus value must retain:

- number of contributing sensors;
- minimum/maximum or spread;
- age of the oldest included sample;
- derivation label such as `LOCAL CONSENSUS`, never `OFFICIAL`.

## RF targets and channel plans

Channel plans are data/model constants or signed regional data, not scattered UI
magic numbers.

### United States NOAA Weather Radio

Seven NFM channels:

- 162.400 MHz
- 162.425 MHz
- 162.450 MHz
- 162.475 MHz
- 162.500 MHz
- 162.525 MHz
- 162.550 MHz

`LISTEN` is a foreground receiver job. `SCAN CHANNELS` samples the seven
channels sequentially and caches relative signal observations. A short scan may
say `NO ALERT DECODED DURING SAMPLE`; it may not say `MONITORING ALERTS`.

A future/dedicated **NOAA RF WATCH** mode may occupy the receiver continuously
for SAME reception. Only that mode may describe itself as continuous RF alert
monitoring.

### Personal weather sensors

Weather Hunter supports regional band profiles. The initial North American
profile considers **315 MHz**, **433.92 MHz**, and **902–928/915 MHz** weather
sensor activity. **868 MHz** is available to regional profiles where
appropriate. **345 MHz** is not scanned by default as a Weather band because it
contains substantial non-weather/security traffic; a decoder may opt into it
when a supported weather protocol requires it.

The UI identifies decoded devices by protocol/model/opaque device ID and RF
band. It does not infer owner identity or physical address.

### Radiosondes

The initial radiosonde discovery range is **400.15–405.99 MHz** (displayed as
`400–406 MHz`). A discovery sweep looks for supported waveform/protocol
candidates; a decoder lock can transition to Balloon Hunter, where the tuner may
remain on that transmitter until the user stops tracking or lock is lost beyond
a bounded timeout.

### Weather satellites

Satellite acquisition is event-driven, not background-polled. The satellite
plan uses locally cached orbital elements and a local target catalog. Initial
137 MHz support is intended for currently operational compatible digital
weather downlinks such as Meteor-series LRPT; frequencies belong to the target
catalog because operational assignments can change.

## Receiver ownership and RF job policy

Weather needs explicit release semantics in the shared receiver-session layer.
The project must not rely on every dashboard voluntarily avoiding the tuner.

Priority order:

1. the newest explicit user receiver action, including a Weather Listen/Scan/Hunter action;
2. an already-running explicit Weather tracking/watch mode;
3. a scheduled event explicitly armed by the user;
4. opportunistic idle-time polling;
5. automatic discovery work.

Rules:

- Opening the Weather dashboard by itself **does not retune** the receiver.
- An explicit Weather Listen/Scan/Hunter action is a user-requested receiver
  takeover and may replace the prior foreground owner through OrcSDR's normal
  controlled stop/restart path.
- Automatic Weather work (scheduled, opportunistic, or discovery) must use a
  non-preemptive claim and must never replace a foreground owner.
- A successful automatic claim is handed intact into the receiver-start path;
  the start path must not discard the claim and call unconditional
  `Session::acquire()` again.
- Dedicated Weather jobs use a distinct Weather owner selected explicitly by the
  adapter. The legacy `owner_for_band(Band::wx) == Owner::radio` mapping remains
  unchanged for existing generic WX callers.
- Leaving an explicit foreground Weather RF mode stops/releases that Weather job.
- Returning Home never leaves a hidden foreground Weather job running.
- A background job aborts rather than preempting a new foreground request.
- Results are cached with age; Home/Weather may display cached results without
  owning the tuner.
- `IDLE ONLY` is the default background policy. `OFF` guarantees no Weather RF
  retunes except explicit user actions.
- SAME completeness is never inferred from opportunistic samples.

The session API therefore needs atomic non-preemptive claim/release semantics in
addition to the existing unconditional foreground `Session::acquire()`. The
claim implementation must publish owner only after band/frequency/rate metadata
is valid and must prevent a check-then-reacquire TOCTOU window.

## Alerts

Alert source is always visible.

### Direct RF

NOAA SAME decoding records the machine-readable header fields actually received,
including event code, location code(s), validity/purge duration, issue time,
and originator/station fields when valid. It stores the raw normalized header
alongside the parsed representation for auditability.

A decoded RF alert may be compared with the configured receiver location/SAME
region, but OrcSDR must preserve the original codes and never silently discard
an alert just because local matching is unavailable.

### Optional network

For the U.S. provider, NWS `api.weather.gov` alerts/forecast endpoints are an
optional enrichment source. Automatic network refresh is disabled by default.
Manual refresh is allowed when Wi-Fi is already available. Cached responses are
identified as cached online data after the connection disappears.

International providers are future implementations behind the same provider
interface; no U.S.-specific API contract belongs in the core Weather model.

## Reports and history

SD is the durable source of truth for Weather reports. NVS may keep small UI
preferences/index hints but is never the only copy of a report.

Base path:

```text
/orcsdr/weather/
  history.jsonl
  reports/
    YYYYMMDD-HHMMSS-<type>-<id>/
```

A normal report bundle contains:

```text
report.html          human-readable, self-contained summary
session.json         structured session metadata and source provenance
observations.csv     normalized weather observations
rf_events.csv        RF tune/sample/decode events
alerts.json          alert records captured in the session
screen.bmp           optional Weather screen capture
map.bmp              optional map/track capture
manifest.sha256      hashes for every file in the bundle
```

Specialized sessions add files such as `telemetry.csv`, `track.csv`, or decoded
satellite imagery. Raw IQ is not part of a normal report.

Writes use `.part`/atomic replacement semantics appropriate to the existing SD
wrapper. A failed save must not destroy the last complete report/index.

On-device history is a bounded summary index derived from `history.jsonl` and/or
report manifests. Missing/corrupt individual reports are shown as unavailable;
they do not prevent browsing other valid entries.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `weather_model.*` | Pure source/freshness/observation/report structs, formatting, regional channel profiles, consensus logic. |
| `weather_dashboard.*` | Five-tab M5GFX rendering, touch/focus behavior, popups, presentation-only state. |
| `weather_runtime.*` | Small orchestration adapter between Weather actions, existing receiver, storage, location, time, map, and optional provider hooks. |
| `weather_report_store.*` | SD report/history load/save, atomic bundle/index handling, HTML/CSV/JSON serialization and hashes. |
| `weather_noaa.*` | NOAA channel plan, local transmitter-catalog matching, scan result model. |
| `weather_rf_scheduler.*` | Foreground/tracking/scheduled/opportunistic job queue and non-preemption policy. |
| `weather_same_decoder.*` | Independently authored SAME signal/header decoder and validation. |
| `weather_sensor_decoder.*` | Protocol registry and normalized weather-sensor observations. |
| `weather_radiosonde.*` | Discovery candidates, supported sonde decode state, telemetry/track model. |
| `weather_satellite.*` | Cached target/orbit/pass state and later LRPT receive/decode integration. |
| `main.cpp` | Narrow hooks only: receiver start/stop/retune, snapshots, navigation, services; no Weather drawing/parsing/report formatting. |

No decoder, SD serialization, map drawing, or Weather tab layout is added to
`main.cpp`.

## OrcDial behavior

Weather remains dashboard wire ID **5**. The dedicated implementation replaces
the current generic Weather placeholder semantics without changing the wire ID.

Initial authoritative controls:

- rotate/semantic previous-next while the NOAA channel card is focused;
- press activates the focused Weather action;
- Home exits;
- volume/mute remain shared controls;
- Weather Hunter list movement is semantic selection, never a generic frequency
  delta.

The Dial must display only state confirmed by the Tab5. If a Weather action is
unimplemented, the Tab5 returns an error instead of acknowledging a fake state.

## Online-enrichment policy

Settings expose:

```text
ONLINE WEATHER ENRICHMENT
  DISABLED              (default)
  MANUAL REFRESH ONLY
  ALLOW AUTO REFRESH
```

The dashboard must not power Wi-Fi merely to fetch weather unless the user has
explicitly enabled a policy that permits it. A manual refresh may request Wi-Fi
through existing services and must surface failure without degrading offline RF
features.

## Validation strategy

### Pure/host checks

- source/freshness classification and stale transitions;
- no zero-as-missing observations;
- consensus input count/spread/age behavior;
- exact NOAA seven-channel plan, including proof that all seven valid channel
  centers survive the WX tune-normalization path instead of collapsing to the
  legacy 162.400 MHz default;
- regional sensor-band profile selection;
- report record validation and escaping;
- SD first-run, round-trip, interrupted-write recovery, malformed history row,
  missing report, and hash manifest behavior;
- RF scheduler priority, try-acquire failure, preemption refusal, release, and
  foreground-over-background behavior;
- SAME parser fixtures and corrupted headers in the Hunter phase;
- radiosonde/sensor decoder fixtures in the Hunter phase;
- pass math/target-catalog fixtures in the satellite phase.

### UI/self-checks

- all five tabs fit 1280x720 with no control below/over the footer;
- every tab is reachable by touch and keyboard focus navigation;
- Weather opens without retuning;
- while WX audio is running, `screen_for_band`, `draw_sdr_screen`, active-screen
  refresh/touch routing, visualizer return, and UI regression all preserve the
  dedicated Weather framebuffer owner instead of falling back to Home/generic Radio;
- RF actions show busy/owned/unavailable states truthfully;
- cached values visibly show age/source;
- missing map pack, SD, location, Wi-Fi, and RTL-SDR each produce explicit
  usable states rather than blank screens;
- global dashboard and documentation truth checks remain green.

### Device acceptance

- native build before flash;
- open/leave Weather repeatedly without unintended receiver retunes;
- NOAA Listen uses existing NFM audio and returns cleanly;
- scan all seven NOAA channels and compare reported strongest result with a
  manual scope check;
- remove/restore SD during report workflows without corrupting prior sessions;
- run Weather with Wi-Fi disabled from boot and prove all core tabs still open;
- run the canonical Tab5 UI regression and verify no audio/USB/DSP regression;
- separately record RF evidence, visual UI evidence, and network-provider
  evidence; one does not stand in for another.

## Research and provenance baseline

Official/current facts used by this design:

- NOAA Weather Radio uses seven VHF-FM channels from 162.400 through
  162.550 MHz: https://www.weather.gov/dsb/nwr
- NWR SAME carries machine-readable alert metadata; NWS SAME/EAS protocol
  material and event codes: https://www.weather.gov/dsb/eventcodes and
  https://www.weather.gov/media/directives/010_pdfs/pd01017012curr.pdf
- NWS radiosondes typically transmit in the 400 to 405.9 MHz range and send
  pressure/temperature/humidity/GPS measurements:
  https://www.weather.gov/upperair/factsheet
- `rtl_433` demonstrates that consumer sensor activity exists across 315,
  433.92, 868, and 915 MHz and provides a useful interoperability/coverage
  reference. OrcSDR does not copy its decoder source:
  https://github.com/merbanan/rtl_433
- NWS API provides forecasts/observations and CAP/JSON alert services, but is
  optional enrichment here: https://www.weather.gov/documentation/services-web-api
- WMO OSCAR lists Meteor-M N2-4 operational with LRPT downlinks in the 137 MHz
  region as of the design date:
  https://space.oscar.wmo.int/satellites/view/meteor_m_n2_4

All protocol implementation work must record its own source/fixture provenance
and licensing review before code is imported or adapted.
