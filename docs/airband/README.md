# OrcSDR Airband Dashboard

The Airband dashboard is a receive-only VHF aviation voice workspace for
118.000–136.975 MHz. It uses AM demodulation and is intentionally separate
from ADS-B: Airband handles voice channels, while ADS-B remains the 1090 MHz
aircraft-data dashboard.

## Dashboard tabs

| Tab | Purpose |
|---|---|
| **LISTEN** | Large tuned-frequency view, measured signal level, squelch state, scan/hold/skip, manual channel stepping, and one-touch 121.500 MHz guard tuning. |
| **SCAN** | Shows scanner state and the active airport/channel bank. The default strategy favors known airport channels when offline FAA data is available; a full-band scan remains available. |
| **AIRPORTS** | Lists nearby/available FAA catalog entries with frequency and service classification. If receiver location is configured, entries are ordered by distance. |
| **ACTIVITY** | Bounded local history of transmissions that opened the scanner, including frequency, duration, peak measured dBFS, and catalog label when known. |
| **SETUP** | 25 kHz / 8.33 kHz raster selection, scan source, squelch threshold, tuner settle time, reply hang time, 121.500 priority watch, and offline-data status. |

## Receiver behavior

Airband is a first-class radio::Band::airband; it no longer routes through
the generic Browse/NFM path. Voice audio uses the existing OrcSDR AM
demodulator with a 10 kHz default receive filter.

The 8.33 kHz tuning raster is calculated as exact thirds of 25 kHz rather than
as repeated 8,333 Hz additions. This avoids cumulative frequency error across
the civil aviation band.

The scanner is receive-only. It supports airport-bank or full-band scanning,
configurable tuner settle time, measured dBFS squelch, hold/resume/temporary
skip, configurable reply hang time, optional periodic 121.500 MHz guard checks,
and a fixed-size in-memory activity log with no heap allocation in the scanner
core.

Continuous-information channels such as ATIS/AWOS are represented by their
catalog service type, but this first implementation does not yet apply a
separate automatic continuous-channel scan policy.

## Offline FAA data

The dashboard consumes the existing OrcSDR aviation catalog format at
/orcsdr/data/faa_aviation.idx.

~~~text
ORCCAT1
ATC <latitude_e7> <longitude_e7> <frequency_hz> <label>
~~~

Labels are classified locally into display categories such as TOWER, GROUND,
APPROACH, DEPARTURE, CENTER, ATIS, AWOS/ASOS, CTAF, UNICOM, CLEARANCE, and
EMERGENCY. Classification is descriptive only; OrcSDR does not claim an
airport/controller identity unless it came from the installed catalog.

When the normal OrcSDR receiver location is configured, the Airband catalog
keeps the closest entries first. Without a data pack, manual tuning and
full-band scanning remain available and the UI explicitly reports that FAA
data is missing.

## Architecture boundary

Airband feature logic lives outside main.cpp:

- airband_scanner.* — channel raster, scan state machine, squelch/hold/hang,
  guard priority, and activity history.
- airband_catalog.* — offline aviation data parsing, service classification,
  distance ordering, and scan-bank construction.
- airband_dashboard.* — 1280×720 M5GFX layout and touch controls.
- airband_runtime.* — settings persistence and the narrow adapter between the
  dashboard/scanner and OrcSDR receiver callbacks.

main.cpp only supplies current receiver state, tuning/navigation callbacks,
receiver-band routing, AM-demod selection, and the UI-loop service call.

## Validation status

The feature branch includes host tests for the channel raster and scanner state
machine in tests/airband_scanner_tests.cpp, wired into
tools/test-radio-scan.sh alongside the existing radio and CB scan tests.

This branch does **not** claim hardware/RF acceptance yet. Before merge or
release, validate on a physical Tab5 + supported RTL-SDR with real aviation AM
traffic, including scan stop/release behavior, 25 kHz and 8.33 kHz tuning,
121.500 priority behavior, audio quality, USB stability, and offline catalog
loading.

## Issue #72 acceptance target

1. Opening **AIRBAND** starts a dedicated AM receiver rather than Browse/NFM.
2. A known local aviation AM transmission is intelligible.
3. Manual 25 kHz and 8.33 kHz channel stepping tunes the expected RF centers.
4. Scan stops on a real transmission, holds through the conversation hang
   interval, and resumes without user intervention.
5. HOLD, RESUME, SKIP, and 121.500 GUARD work from the touchscreen.
6. With the FAA pack installed, airport/channel rows tune the selected
   frequency and do not fabricate identities when a match is unavailable.
7. A sustained receive/scan soak shows no new USB drop or audio regression.
