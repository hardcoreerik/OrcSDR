# OrcSDR Airband Dashboard

The Airband dashboard is a receive-only VHF aviation voice workspace for
118.000–136.975 MHz. It uses AM demodulation and is intentionally separate
from ADS-B: Airband handles voice channels, while ADS-B remains the 1090 MHz
aircraft-data dashboard.

## Location is shared OrcSDR state

Airband does not own a location and does not depend on ADS-B settings for one.
The configured receiver position is canonical OrcSDR state shared by the
location picker, OrcMaps/map selection, ADS-B geometry, Airband proximity
queries, and future location-aware radio databases.

If no receiver location is configured, Airband still supports manual tuning,
full-band scanning, and one-touch 121.500 MHz guard reception. Airport-bank
scanning and database-derived airport/controller labels are disabled until a
location is set. This prevents a national or global database from presenting
an arbitrary frequency record as if it were local RF identity.

## Dashboard tabs

| Tab | Purpose |
|---|---|
| **LISTEN** | Large tuned-frequency view, measured signal level, squelch state, scan/hold/skip, manual channel stepping, and one-touch 121.500 MHz guard tuning. |
| **SCAN** | Shows scanner state and the active airport/channel bank. The default strategy favors nearby known channels when aviation data and receiver location are available; a full-band scan remains available. |
| **AIRPORTS** | Lists nearby catalog entries with frequency, service class, distance, provenance class, and database label. |
| **ACTIVITY** | Bounded local history of transmissions that opened the scanner, including frequency, duration, peak measured dBFS, and catalog label when known. |
| **SETUP** | 25 kHz / 8.33 kHz raster selection, scan source, squelch threshold, tuner settle time, reply hang time, 121.500 priority watch, and offline-data status. |

## Receiver behavior

Airband is a first-class `radio::Band::airband`; it no longer routes through
the generic Browse/NFM path. Voice audio uses the existing OrcSDR AM
demodulator with a 10 kHz default receive filter.

The 8.33 kHz tuning raster is calculated as exact thirds of 25 kHz rather than
as repeated 8,333 Hz additions. This avoids cumulative frequency error across
the civil aviation band.

The scanner is receive-only. It supports airport-bank or full-band scanning,
receiver-safe tuner settle time, measured dBFS squelch, hold/resume/temporary
skip, configurable reply hang time, optional periodic 121.500 MHz guard
checks, and a fixed-size in-memory activity log.

Continuous-information channels such as ATIS/AWOS are represented by their
catalog service type, but a dedicated continuous-channel scan policy remains a
follow-up item.

## International aviation data

The preferred runtime path is:

~~~text
/orcsdr/data/aviation.idx
~~~

OrcSDR also reads the legacy U.S. path for backward compatibility:

~~~text
/orcsdr/data/faa_aviation.idx
~~~

Two schemas are accepted.

### ORCAIR2 — global/provenance-aware

~~~text
ORCAIR2
COM<TAB>lat_e7<TAB>lon_e7<TAB>frequency_hz<TAB>service<TAB>country<TAB>airport_ident<TAB>airport_name<TAB>callsign<TAB>source_class<TAB>source_name<TAB>label
~~~

Source classes are `OFFICIAL`, `LICENSED`, `COMMUNITY`, `USER`, `DERIVED`, or
`UNKNOWN`. They describe provenance; they are not a claim that the currently
heard RF transmission was positively identified.

`tools/data_catalog/build_ourairports_aviation_index.py` converts the public-
domain OurAirports `airports.csv` and `airport-frequencies.csv` exports into
ORCAIR2 country, region, or radius-limited packs. OurAirports records are
marked `COMMUNITY`. Official national AIP/eAIP importers can emit the same
schema as `OFFICIAL`, allowing authoritative records to enrich or replace the
baseline without changing the firmware data model.

### ORCCAT1 — legacy FAA compatibility

~~~text
ORCCAT1
ATC <latitude_e7> <longitude_e7> <frequency_hz> <label>
~~~

Legacy rows are treated as official FAA provenance. Existing packs continue to
work unchanged.

## Database identity versus measured RF

OrcSDR keeps measurement separate from interpretation. A station card can say:

~~~text
124.900 MHz AM
Measured signal: -58 dBFS
Database context: KEUG TOWER, 8.3 NM
Source: OFFICIAL / FAA
~~~

It must not claim that a voice transmission belongs to a particular airport,
controller, or aircraft merely because the tuned frequency matches a nearby
database record. Reused frequencies make location/provenance context essential.

## Architecture boundary

Airband feature logic lives outside `main.cpp`:

- `airband_scanner.*` — channel raster, scan state machine, squelch/hold/hang,
  guard priority, and activity history.
- `airband_catalog.*` — ORCAIR2/ORCCAT1 parsing, service/provenance metadata,
  distance ordering, and scan-bank construction.
- `airband_dashboard.*` — 1280×720 M5GFX layout and touch controls.
- `airband_runtime.*` — settings persistence and the narrow adapter between the
  dashboard/scanner and OrcSDR receiver callbacks.
- `receiver_location.*` — canonical in-memory OrcSDR receiver position shared
  by location-aware features.

`main.cpp` remains integration glue: it supplies current receiver state,
tuning/navigation callbacks, receiver-band routing, AM-demod selection, and
the UI-loop service call.

## Data build example

A country pack from OurAirports can be generated offline with:

~~~bash
python tools/data_catalog/build_ourairports_aviation_index.py \
  --airports airports.csv \
  --frequencies airport-frequencies.csv \
  --country SG \
  --output aviation.idx
~~~

A receiver-area pack can instead use `--center-lat`, `--center-lon`, and
`--radius-nm`. Release builds should archive the exact source snapshot and
license/provenance used to generate the runtime pack.

## Validation status

The branch includes host tests for scanner behavior and data-normalizer tests
for ORCAIR2 generation. Hardware/RF acceptance still requires a physical Tab5
and supported RTL-SDR with real aviation AM traffic. Validate scan stop/release
behavior, 25 kHz and 8.33 kHz tuning, 121.500 priority behavior, audio quality,
USB stability, location changes, and both ORCAIR2 and legacy ORCCAT1 loading.
