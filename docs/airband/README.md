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
demodulator. The channel filter follows the channel spacing: 10 kHz for 25 kHz
channels and 6 kHz for 8.33 kHz channels (adjacent 8.33 kHz channels would
overlap a wider filter).

### Squelch: carrier versus noise floor

Airband squelch does **not** use the absolute wideband IQ power. That number is
the power of the whole 2.4 MS/s capture, is dominated by noise, and is held
roughly constant by the tuner AGC, so it cannot tell an empty channel from a
busy one (an earlier design pinned the scanner on the first channel for exactly
this reason). Instead the runtime tracks the in-channel carrier level (the mean
envelope of the channel-filtered IQ that the AM demodulator already computes)
and a noise floor (fast to follow drops, slow to follow drift, never chasing a
real carrier). The squelch opens when the carrier is the configured number of
dB above that floor (default +4 dB, `0` = always open) and closes 2 dB lower.
The floor is re-learned after every retune, gain change, and channel-width
change. Displayed levels are therefore "SNR in dB", not dBFS.

### Gain

**Gain is kept across reboots.** A boot turns the tuner AGC back on, and in AGC mode the driver chose a
gain that raised the in-channel noise floor about 8 dB (about -29 dBFS against -37.5 dBFS at a fixed
40.2 dB on the bench), burying weak transmissions. Airband therefore saves the gain mode and value you
choose and re-applies it as soon as the receiver is running; with nothing saved it uses a fixed manual
gain of about 40 dB. Pick **TUNER AGC ON** to opt back in; that choice is remembered too.

**Audio filter.** The demodulated audio passes a voice band-pass (about 300 Hz to 3.2 kHz) because the AM
detector's hiss is wideband while speech is not.

The LISTEN tab exposes RF gain +/-, tuner AGC, and RTL AGC through the shared
`receiver_controls` model, the same driver calls the Shortwave and AM
dashboards use. Controls that a receiver does not support are shown as
unavailable, not hidden. Airband adds no gain logic of its own.

### Nearby airports and radius

Nearby lookup is bounded by a radius (25, 50, 100, 250, 500 nm, or any;
default 100 nm) set on the AIRPORTS tab. Entries outside the radius are dropped
while the catalog streams, so a worldwide file never has to fit in memory: at
most 32 of the nearest records are kept. With no receiver location the file is
not read at all.

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
- `airband_catalog.*` — ORCAIR2/ORCCAT1 parsing over a `LineSource`,
  service/provenance metadata, distance/radius filtering, and scan-bank
  construction. `airband_catalog_storage.cpp` is the only part that touches the
  SD card (chunked reads); everything else is host-testable.
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

**Host tests** (`tools/test-radio-scan.sh`, run under ASan/UBSan and in CI):
channel raster and 25 kHz / 8.33 kHz stepping, scanner state machine
(hold/hang/skip/guard priority), carrier-versus-noise-floor squelch, gain-step
table, radius choices, ORCAIR2 and legacy ORCCAT1 parsing, malformed rows,
missing/empty/bad-header input, CRLF, duplicates, nearest-N selection,
radius filtering, no-location behaviour, great-circle distance (including far
longitudes and the antimeridian), incompatible-schema early exit, and bulk
far-row rejection. `tests/test_data_catalog.py` covers the ORCAIR2 generator.

**Hardware (M5Stack Tab5, RTL-SDR Blog V4, 2026-09-29/30):**

- Enter Airband from the Serial UI/Home path; dashboard loads, receiver
  streams, no crash or watchdog.
- Manual tuning 118.000 to 136.975 MHz, 25 kHz steps, exact 8.33 kHz steps
  (118.008333, 118.016667, 118.025), band-edge clamping, out-of-band rejection,
  121.500 guard shortcut. Channel filter follows the spacing (10 / 6 kHz).
- RF gain +/-, tuner AGC and RTL AGC through the shared controls; results agree
  with `RTL_GAIN STATUS`.
- Squelch: an idle channel reads about 0 to 3 dB above the floor and stays
  closed at +8 and +30 dB; 0 dB is always open.
- Full-band and airport-bank scanning advance across channels; hold and resume
  work; skip is accepted; scan stops cleanly. On live RF the scanner stopped
  and held on 121.875 MHz at 6 to 9 dB SNR.
- Location: with none configured nothing is read from the SD card and no
  "nearby" airport or label is shown. With a test location set, the worldwide
  OurAirports pack (27,758 records, 3.2 MB) yields 32/29/13 entries at 100/50/25
  nm and 32 with no radius; database matches (e.g. `KEUG`) appear only on
  catalog frequencies and never on others.
- Leaving for Home, FM, AM and Shortwave and re-entering Airband releases and
  reacquires the receiver with no crash; heap and DMA minimums stayed flat.

**Live voice, 2026-09-30 (M5Stack Tab5, RTL-SDR Blog V4 and V3c/V4L spot checks, passive GA-800 loop,
Eugene OR, about 9 nm from KEUG):** transmissions on 119.600 MHz (Cascade Approach/Departure) were received
and played from the device speaker, confirmed by ear against the LiveATC stream of the same frequency.
Evidence: at 17:52:03 the in-channel level rose about 8 dB for about 5 s (SNR to 9.6 dB) exactly when the
operator heard traffic on the stream; the NOAA weather channel at 162.400 MHz measured +14 dB over empty
spectrum as a VHF reference; a speaker tone test confirmed the output path. Two things hid the signal until
they were found: the tuner AGC (see Gain above) and an over-strict squelch. Real signals were only 6 to 9 dB
over the noise, so antenna placement still matters.

**Not validated / limitations:**

- Reception of a real voice transmission and audio quality could not be judged
  by serial: the scanner hold on 121.875 MHz shows the carrier path works, but
  intelligibility, AM audio level and squelch tail need a listening test with
  live traffic. Whether +8 dB is the right default in different RF
  environments needs field data.
- ATIS/AWOS continuous channels have no dedicated scan policy.
- Loading a worldwide pack takes about 2.5 to 3 seconds on the UI thread (it
  runs when Airband is entered with a location, when the radius changes, and
  on Reload). It is bounded in memory (32 entries) but not yet off-thread.
- The FAA record-index pack installed by Data & Maps has no coordinates and is
  reported as an unsupported format; an ORCAIR2 `aviation.idx` is required for
  nearby-airport features. There is no published ORCAIR2 pack yet.
- Opening the ADS-B dashboard on the bench can reset the device through the
  task watchdog; this reproduces on `main` and is tracked in issue 131.
