# Shortwave Dashboard Phase 2 Design

## Goal

Complete the Shortwave Explorer's visible product flow: listen, discover,
hunt, save, log, and export. The feature must remain a standalone Shortwave
module, must not grow `main.cpp` into a dashboard implementation file, and
must preserve listening history across an M5Burner update that rewrites NVS.

This design extends, but does not rewrite, the accepted Phase 1 record in
`docs/superpowers/specs/2026-09-14-shortwave-dashboard-design.md`.

## Scope

- Repair the Live direct-frequency keypad so it is a modal surface: periodic
  dashboard updates cannot overpaint its field, keys, Cancel, or Tune buttons.
- Accept exact direct entry from 24 kHz through 30 MHz, matching the integrated
  driver's arithmetic range, and label the tuned region as VLF edge, LF, MF,
  or HF instead of calling every frequency a shortwave broadcast band.
- Replace the visible `LATER` placeholders with functional On Air, Hunt,
  Memory, and Logbook tabs.
- Store Shortwave memories and reception logs on the Tab5 SD card. SD is the
  durable source of truth; NVS is not a logbook backup.
- Export log records as UTF-8 CSV and a conservative ADIF `.adi` interchange
  file. CSV is the complete OrcSDR record; ADIF contains only standard fields
  that have a truthful equivalent plus the listener details in `COMMENT`.
- Reuse the existing radio session, FFT/spectrum, scan engine, time service,
  and SD storage wrapper. No second receiver, DSP, database, or networking
  subsystem is introduced.

## Non-goals

- No cloud account, cloud synchronization, Wi-Fi schedule download, or station
  identity claim based only on signal strength.
- No change to the RTL-SDR driver, tuner routing, demodulator, audio path,
  or existing AM/FM dashboard behavior.
- No new SSB, CW, NFM, WFM, or DRM demodulator in this dashboard phase. The UI
  may recommend an unsupported mode, but it must not draw an actionable mode
  button until the receiver can actually demodulate it.
- No guarantee of reception without an antenna suitable for the selected band.
- No NVS migration or promise that arbitrary old NVS log data can be restored.

## Product behavior

### Live

Live remains the listening surface. Tapping the frequency opens a Shortwave
owned modal keypad. While the keypad is open, `update()` records the latest
snapshot but returns before drawing any underlying Live widgets. Key presses
redraw only the keypad. Cancel restores Live without tuning; a valid Tune
closes the keypad and emits exactly one `tune_hz` action.

The keypad accepts `0.024000` through `30.000000` MHz and rejects values
outside that range with an inline error; it never silently clamps a user's
entry. The frequency card distinguishes:

- `24-30 kHz`: VLF edge (driver coverage; reception remains unverified).
- `30-300 kHz`: LF.
- `300 kHz-3 MHz`: MF, including longwave/medium-wave broadcasting where
  regionally applicable.
- `3-30 MHz`: HF, conventionally called shortwave.

The card also provides a contextual `MODE GUIDE`, not an automatic identity
claim. Locally known broadcast allocations recommend AM. Amateur voice
segments may recommend LSB below 10 MHz and USB above 10 MHz, with the 60 m USB
exception; aeronautical and maritime HF voice may recommend USB. CW, DRM,
digital, and the limited 10 m FM usage are identified only when local band-plan
metadata supports the hint. WFM is never recommended below 30 MHz. Unknown
space says `MODE UNKNOWN - START WITH AM`.

For Phase 2, AM remains the only actionable Shortwave mode. Unsupported mode
hints explain that receiving them requires a later demodulator rather than
offering a control that cannot work.

The existing receiver capability controls remain capability-aware. A V3c in
direct-Q mode does not advertise unavailable tuner gain; a V4 HF-upconverter
route continues to expose its supported controls.

### On Air

On Air uses only local schedule/catalog entries. It filters entries by the
current UTC minute, day mask, selected shortwave band, and receiver frequency
tolerance. Results use the honest labels `ON AIR`, `LIKELY`, or `SCHEDULED`;
they never declare that the receiver decoded a station identity. If a catalog
is absent or has no qualifying entry, the tab says so plainly and offers a
return to Live. Selecting a result emits one exact-frequency tune action.

### Hunt

Hunt wraps the existing scan engine instead of implementing another scan loop.
The user selects a broadcast band, starts or stops the scan, sees ranked peak
candidates, and taps a candidate to tune it. A candidate is an RF observation,
not a station identification. Leaving Hunt cancels its active scan and restores
the normal Shortwave listen state.

### Memory

Memory supports Save Current, Tune, Edit Label/Notes, Favorite, and Delete.
Each record preserves exact frequency, bandwidth, AM mode, timestamp, and
optional station metadata. All mutations write the SD library atomically;
failure leaves the prior valid file intact and produces an explicit SD error
state on the tab.

### Logbook

Logbook supports New Entry from current listening state, browse, edit, delete,
and export. A new entry pre-fills UTC, exact frequency, bandwidth, AM mode,
measured signal, receiver device, and the user-selected antenna text. Station
and reception notes remain optional. The visible save result distinguishes
`Saved to SD`, `SD unavailable`, and `write failed`.

Export creates dated files under `/sd/OrcSDR/exports/`:

- `orcsdr-shortwave-YYYYMMDD.csv`: all OrcSDR fields and explicit headers.
- `orcsdr-shortwave-YYYYMMDD.adi`: UTF-8 ADIF records using truthful core
  fields (`QSO_DATE`, `TIME_ON`, `FREQ`, `MODE`) when present; station,
  antenna, signal, device, and notes are represented as human-readable
  `COMMENT` content rather than invented QSO/contact data.

The ADIF export is independently authored from the published ADIF 3.1.7
format; no ADIF reference implementation is copied. The format is an
interchange convenience for logbook users, not a claim that a broadcast
reception is an amateur-radio two-way contact.

## Module boundaries

| Module | Responsibility |
| --- | --- |
| `shortwave_dashboard.*` | Tab rendering, modal ownership, touch-to-action mapping, and presentation-only state. |
| `shortwave_library.*` (new) | SD paths, bounded load/save, atomic replacement, memory/log CRUD, CSV/ADI serialization, and storage error state. |
| `shortwave_hunt.*` (new) | Shortwave-band scan state and conversion of existing scan-engine observations into touchable candidates. |
| `shortwave_model.*` | Band rules, data-contract validation, schedule matching, and pure formatting helpers. |
| `main.cpp` | Small adapter only: builds a snapshot, starts/stops the existing scan service, calls Shortwave actions, and forwards result snapshots. |

No dashboard UI code, SD file parsing, export formatting, or scan ranking is
added to `main.cpp`.

## Durable SD format and recovery

The Shortwave library owns `/sd/OrcSDR/shortwave/`:

- `memories.csv` is the complete memory library.
- `logbook.csv` is the complete reception library.
- `memories.tmp` and `logbook.tmp` exist only while replacing their matching
  file.

Each write is: serialize a complete bounded file to `.tmp`, flush and close,
rename the existing file to `.bak`, rename `.tmp` to the final name, then
remove `.bak`. On boot, if the final file is absent and `.bak` exists, restore
the backup. A malformed row is ignored and counted; a malformed whole file is
not overwritten until the user makes a deliberate save. Record counts and text
lengths are bounded to fixed embedded-friendly limits.

The SD card must be mounted before the library exposes write actions. Reads,
deletes, exports, and writes fail closed when it is unavailable. NVS retains
only the last selected tab, band, and transient UI choices; it is never the
sole copy of a memory or reception log.

## Data flow

```text
Tab5 touch
  -> shortwave_dashboard::Action
  -> small main.cpp adapter
  -> shortwave_library / existing scan engine / existing receiver action
  -> Snapshot or result collection
  -> shortwave_dashboard draw
```

The periodic receiver snapshot continues while a keypad is displayed, but
Live redraw is suppressed until the modal closes. Spectrum/waterfall rendering
is likewise suppressed under the keypad; it resumes from current live data on
return.

## Validation

### Host and self-checks

- Direct-frequency modal survives a periodic `update()` and still recognizes
  Cancel, digit, and Tune touch regions.
- Direct entry accepts the exact 24 kHz and 30 MHz endpoints, rejects values
  outside them without tuning, and reports the correct VLF/LF/MF/HF region.
- Mode guidance tests cover broadcast AM, customary amateur LSB/USB including
  the 60 m exception, utility USB, limited 10 m NFM, unknown space, and the
  absence of any WFM recommendation below 30 MHz.
- Band selection, frequency ranges, schedule-time matching, scan-candidate
  ranking, memory/log validation, CSV escaping, and ADIF field formatting have
  focused deterministic checks.
- SD library checks cover first run, write/read round-trip, failed/absent SD,
  atomic replacement recovery, malformed-row rejection, and export files.
- Existing dashboard/self-check and Tab5 UI regression continue to pass.

### Native and device acceptance

- Build native Tab5 firmware before flash.
- Direct Tune opens cleanly, accepts a valid frequency, cancels cleanly, and
  remains usable after periodic screen refreshes.
- Each tab opens, returns to Live, and has no overlapping controls at 1280x720.
- Hunt scans and a selected candidate tunes; it is recorded as an RF candidate,
  not a confirmed station.
- Save a memory and a log entry, power-cycle the Tab5, and confirm both reload
  from SD. Do not use an M5Burner reflash as the first test because it changes
  more than the storage boundary.
- Export CSV and ADI, then verify the files exist and have the expected record
  count. The user can inspect the SD card independently.
- Record connected antenna and its band suitability for every audible reception
  claim. Source, build, flash, serial, raw spectrum, audible radio, and
  physical UI remain separate evidence claims.

## Research and provenance

- ADIF 3.1.7 is the current released Amateur Data Interchange Format and
  publishes its specification/resources for interoperability. OrcSDR will use
  it as a format reference only, with independently written serialization:
  https://www.adif.org.uk/317/index.htm
- Phase 1's ITU broadcast-band source remains the authority for the existing
  shortwave broadcast-band table:
  https://www.itu.int/en/ITU-R/terrestrial/broadcast/Pages/bands.aspx

No external code or data is copied into this feature solely from either source.
