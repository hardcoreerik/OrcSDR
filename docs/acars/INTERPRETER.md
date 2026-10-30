# ACARS message interpreter

Status: **host-tested, built into the firmware (`apps/orcsdr-tab5/main/CMakeLists.txt`) and run on a Tab5 with live traffic.** Code: `apps/orcsdr-tab5/ui/acars_interpret.{hpp,cpp}`,
tests: `tests/acars_interpret_tests.cpp` (run by `tools/test-acars-ui.sh`, with ASan/UBSan).

The decoder delivers a CRC-verified block; this module turns its label and text into plain language. It is a pure function over a
`MessageRecord` (no allocation, no hardware, no state), computed when a row or the detail page is drawn, so it adds no per-record RAM
(the Tab5 internal RAM boot pool is nearly full; see the project notes). The raw text is shown beside it by default; the `RAW TEXT IN DETAIL` setting (SETUP > RAW TEXT) can hide it on the detail page. A field that is not
understood is left out or shown as received, never given a name.

On screen: in the LIVE list the interpreted summary is drawn **cyan**, raw text stays white; a block whose text layout is not
understood (or whose label is merely named) keeps its raw text in the list. The detail page's APPLICATION INTERPRETATION panel shows a
headline (green = text parsed, amber = only the label is known) and up to four decoded lines.

## What is decoded

Sources: ARINC Specification 620 (squitter, general response, autotune, message sequence number, H1 sub-labels, label table C-1),
the community research in `airframesio/acars-message-documentation` (facts only, no license is shown on that repository), and formats
observed on 131.550 MHz and cross-checked against each other. Everything is written from field layouts, nothing is copied. The ARINC
documents are the publisher's and are not stored in this repository.

| Label / text | Result | Basis |
|---|---|---|
| `SQ` squitter v0, v1, v2 | station IATA/ICAO and number, provider, position, VDL2 service frequencies, free text | ARINC 620 4.2.5, including its examples |
| `_<DEL>` (shown `_?`) | acknowledgement, and which block it confirms | ARINC 620 4.2.1; direction from the block id (letter = uplink, digit = downlink) |
| `:;` | retune to a frequency, optional seconds | ARINC 620 4.2.6 |
| `Q0`, `_d` | link test, link ping | ARINC 620 / community |
| `H1` + `#xx` sub-label | which on-board unit (DFDAU `DF`, left/right/centre/selected FMC `M1`/`M2`/`M3`/`MD`, `CF`, `EI`, `WO`, ...); the `B` after it is the reserved byte | ARINC 620 Table C-1 and 4.4.4 |
| `H1` `#M1B` `POS...` (FMC position) | position, altitude, waypoint and time, next waypoint ETA, temperature, wind | observed, Alaska and others; community POS page agrees on the field order |
| label `21` `POS...` (Frontier style) | position, altitude, report time, temperature, ETA and destination | one aircraft type observed |
| `H1` `#DF` (flight data unit) | cruise fixes (decimal degrees, day/time, altitude, temperature, wind, phase), `TRP#` position blocks, route and report header | observed (Alaska, and a `KPDX`-`KLAX` report); not in any public documentation found |
| label `32` (one regional carrier) | date, time and position; ten other numbers shown as received | observed live on VDL2 (five samples); see `captures/2026-10-29-vdl2-136975/README.md` |
| label `33` (one regional carrier) | date, time, position, altitude (probable) and the two airports; three other values shown as received | two live samples 2026-10-29; fix track fits KMFR to KSEA; not yet matched to a web track |
| `H1 #DFB` header block (Southwest style) | aircraft type, date, flight, origin and destination | live, WN0436; origin and destination match OpenSky (KEUG departure) |
| `H1 #DFB` sample blocks (Southwest style) | time, altitude, calibrated speed, Mach; other columns not named | live; speed and Mach agree with the standard atmosphere at the printed altitude (six samples within 0.001) |
| `H1 #M1B/BA <ground>.AT1.<tail><hex><crc>` (ARINC 622) | direction, ground address, application, tail, data length, checksum; the data bits are shown as received | CRC-16 (0x1021, start 0xFFFF, final complement) matches the ARINC 622 examples and 5 live blocks; blocks failing it are not decoded |
| label `B9` `/KSEA.TI2/024KSEAA3BC3` (D-ATIS request, ARINC 623 appendix B) | airport, request type, display width, checksum | same CRC; 6 samples pass (2 live from this unit, 4 from community notes) |
| `H1` `#CFB` `FDE...` (central fault display) | report time, tag, and the event entries (time + code, as received) | observed live on VDL2 (three messages); code meanings not published |
| `AV` (VDL2 data frame, not ACARS) | source address; raw hex only | produced by the VDL2 receiver for non-ACARS information frames |
| message header | message number (originator, sequence, block letter), flight id, airline name for AS F9 WN DL AA UA B6 NK HA G4 QX OO AC FX 5X YX MQ OH SY | ARINC 620 message layout; airline codes are conventional IATA |
| about 50 other labels | a plain-English name only | ARINC 620 Table C-1 rows that were unambiguous, plus community research |

## Cross-checks that settled real questions

- **Latitude/longitude in `#M1BPOS` are degrees plus minutes with one decimal** (`N44397` = 44 deg 39.7 min = 44.662). The community
  page reads such values as thousandths of a degree. Alaska 536 sent `N44397W122192` at 20:27:50Z and a flight-data-unit block at
  20:28:03Z with 44.6863, -122.3252: degrees plus minutes agrees to about 1.5 nm in 13 s; thousandths would be a 17 nm jump. The
  flight-data-unit fixes themselves are decimal degrees (consecutive fixes two minutes apart move 0.248 deg, about 446 kt).
- The time field of a label-21 report (`202236`) matched the receive time (20:22:39Z). Squitter, acknowledgement and autotune layouts
  match the ARINC 620 text and examples.
- `#DFB` is not a "sub-message type": `DF` is the sub-label (DFDAU) and `B` the reserved byte.

## Deliberately not done (and why)

- **Multi-block reassembly.** The message number's last character is the block letter (`D47A` to `D47G` is one seven-part report); all
  parts share `D47` and the flight. Joining them needs a store across records, which this pure function does not have.
- **Matching an acknowledgement to the downlink it confirms** (needs the same cross-record view).
- **Engine and fault blocks** inside `#DF` reports, label `24`, `4N`, `80`-`89`, `5Z` and airline-designated formats: no documented
  layout; the unknown numbers stay unnamed until a source or enough samples exist. Delta, American and Southwest formats were not
  found in any public documentation, so they appear only if captured.
- **Aircraft type from the tail number** needs a registration or ADS-B source.

## Adding a parser

Write it from a documented or repeatedly observed layout, validate every field's shape (an unmatched shape means "not decoded", never
a guess), keep output within `kInterpLines` x `kInterpLineChars`, and add the captured block verbatim to
`tests/acars_interpret_tests.cpp` with the decoded values it must produce. The hostile-input loop in that file covers bounds.
