# ACARS format documentation research (2026-10-29)

Scope: documentation only. No decoder source (dumpvdl2, vdlm2dec, acarsdec, libacars, acarshub) was read or copied. Samples are the
live VDL2 captures from 136.975 MHz listed in the task. Web pages and PDF text were treated as data.

## Sources and how far to trust them

| Id | Source | Licence / status | Used for |
|---|---|---|---|
| S1 | ARINC Specification 622-4 (2001), "ATS Data Link Applications over ACARS Air-Ground Network", read from an unofficial mirror: https://ftp.yuuta.moe/Library/Aviation/ARINC/ARINC%20SPECIFICATION%20622/ARINC%20SPECIFICATION%20622-4-2001.pdf | Copyright ARINC/SAE ITC. Not licensed for redistribution; the mirror is not the publisher. Only facts and short field names are quoted here. | gap 7 |
| S2 | ARINC Specification 620-4 (1999), "Datalink Ground System Standard and Interface Specification", same mirror | Same | gaps 6, 8, 9 (label meanings) |
| S3 | ARINC Specification 623-3 (2005), "Character-Oriented Air Traffic Service Applications", same mirror, Appendix B | Same | gap 9 |
| S4 | airframesio/acars-message-documentation, https://github.com/airframesio/acars-message-documentation (`research/17.md`, `32.md`, `33.md`, `37.md`, `B9/`, `5Z/`, `H1/`) | **No licence shown** on the repository. Community notes, many pages are empty templates ("..."). Facts only. | all gaps |
| S5 | The project's own captures, `docs/acars/captures/2026-10-29-vdl2-136975/README.md` | ours | cross-checks |

Mirror caveat: S1-S3 are the publisher's paid documents obtained from a third-party mirror. They are already cited in
`docs/acars/INTERPRETER.md`. Nothing below needs the owner to redistribute them; if this repo is ever made public the citations
should point at the ARINC catalogue (ARINC 622-5 2017, 620, 623) rather than the mirror. I recommend the owner buys the current
editions if these decoders ship.

Method notes: the S4 pages were summarised by a small model, so they are second-hand; re-read the raw pages before relying on a
field name. S1-S3 text was extracted locally with pypdf and read directly, and the examples were reproduced with a short scratch
script (not committed, not decoder code taken from anywhere: it is the spec's own CRC description).

## Executive summary

| Gap | Result | Safe to name fields? |
|---|---|---|
| 7 ARINC 622 `AT1` | **Fully documented and verified on the live sample** (structure and CRC) | Yes, structure and CRC. Payload bits need RTCA DO-258/DO-219 or EUROCAE ED-100 (not obtained) |
| 9 `B9 .TI2/` | **Fully documented and verified on 5 of 5 samples** (ARINC 623 App. B plus CRC) | Yes |
| 9 `5Z` | Documented as "airline designated downlink", free text. Content is airline-private | Label name yes; content as received |
| 6 `#CF` | Sub-label identity documented (ARINC 620); the `FDE...` text layout is not public | Sub-label yes; fields no |
| 8 label `37` (WN) | Not a documented encoding. Only community note: "appears obfuscated" | No |
| 1-3 `#DFB`, `#DFBTRP`, `#DFBA86...` | No public format. Physics can name a few columns, as hypotheses | Only what is already verified |
| 4-5 labels 32/17/33 | Community pages agree on date/time/position and little else | Already at the limit |

Two corrections to our own notes came out of this (see "Corrections" at the end): the community repository expands `#DFB` and
`#CFB` as "Digital/Crew Flight Bag", which contradicts ARINC 620; and `.TI2/` is not an Alaska-specific code at all.

---

## Gap 7: `#M1B/BA USADCXA.AT1.N926WN6082D826032E1F` (ARINC 622 ATCComm)

**Documented (S1, confidence high; verified on our sample).**

- Label `BA` is the downlink "ATCCommunications" label/MFI (S2 5.5.11 and S1 4.4.1: "'AA' for uplinks, 'BA' for downlinks").
  The `H1` label with `#M1B` is the sub-label wrapper (FMC, the unit that originated it); the text after it is the ARINC 622 message.
- Layout (S1 4.4.3, examples `/AA OAKXGXA.AT1.N678DL4104532300A9B3`):

  `/` `MFI` ` ` `<7-char ground address>` `.` `<IMI>` `.` `<registration, 6-7 chars>` `<hex application data>` `<4 hex CRC>`

  For our sample: MFI `BA`, ground address `USADCXA` (an ATC centre address; 7 characters, the same form as the spec's `OAKXGXA`),
  IMI `AT1` ("ATCComm application, Version 1" in the spec's table), registration `N926WN`, data `6082D82603` (10 hex digits =
  40 bits), CRC `2E1F`.
- IMIs for this application (S1 4.4.1): `CRx` connect request (uplink), `CCx` connect confirm (downlink), `ATx` data (both
  ways), `DRx` disconnect; `x` is the version. `AFN` (logon, label B0/A0), `ADS` (reports, A6/B6) and `DIS` (disconnect) are the sibling
  IMIs. Spec examples: `/OAKXGXA.AFN/FMHDL001,.N678DL,A8F9C6,`; `/B6 OAKXGXA.ADS.N678DL032D2633`.
- Hex data is the bit stream cut into 4-bit pieces and written as ISO-5 hex characters (S1 2.2.4). The registration is
  kept in the text so the receiver can confirm correct delivery.
- **CRC (S1 2.2.3 and Appendix B).** Polynomial `x^16 + x^12 + x^5 + 1` (the ARINC 429 polynomial), data MSB first, first two octets
  complemented, remainder complemented, computed over IMI + `.` + registration + the data **bytes** (hex digits packed to bits).
  I implemented that description and it reproduces all three of the spec's own examples (`ADS.N678DL`+`072D` -> `EAF7`,
  `AT1.N678DL`+`4104532300` -> `A9B3`, `ADS.N678DL`+`032D` -> `2633`) **and our live block: `AT1.N926WN` + `6082D82603` -> `2E1F`**.
  This is a free integrity check that rejects false parses, independent of the VDL2 frame CRC.

**Hypothesis (confidence low).** The 40 data bits are a FANS-1/A CPDLC downlink in the DO-219 / ED-100 bit encoding (message element
ids, then arguments, PER-aligned). `6082D82603` would be a short downlink (a few elements; for example a WILCO/UNABLE style reply or a
position/level report). Not decodable without the element table. S1's example `4104532300` is a WILCO downlink pair in the spec
(S1 4.4.3.2 shows its bit breakdown), so the spec is a source of worked examples for testing.

**Missing source.** The element table is in RTCA DO-258A / EUROCAE ED-100A (FANS-1/A interoperability) and ICAO Doc 4444 / Doc
9694 (CPDLC message set). Not obtained here; commercial documents.

**Test against live.** (1) Parse and CRC-check every `BA`/`AA`/`B0`/`A0`/`A6`/`B6` H1 block; report "ARINC 622 ATCComm, CRC ok,
ground address X, registration Y, N data bytes" and nothing else. (2) The registration must equal the ACARS header's registration
and the ground address should be stable per ATC centre across flights; mismatch means a bad parse. (3) For `AFN` messages
(plain text, no bits) the fields are fully readable from S1 3.5: `FN_CON`, `FN_CAD`, `FN_AK`; decode these immediately.

**Name fields safely:** ground address, IMI and version, registration, data length, CRC pass/fail, AFN text. **As received:** the
hex payload.

---

## Gap 9a: `B9` `.TI2/` (`L03AAS0608/KSEA.TI2/024KSEAA3BC3`)

**Documented (S2 5.5.9, S3 Appendix B Table B-1/B-3, confidence high; verified 5 of 5).**

- Label `B9` is "Request ATIS Report" (D-ATIS request, downlink; the service provider translates it to SMI `RAI`).
  `TIx` is the IMI of the ATIS request/report; `x` is the version, and **`TI2` is version 2, "Preferred"** (S3 Table B-1).
  So `.TI2/` is not Alaska specific; it is the standard D-ATIS version 2.
- Text layout: `/` `<station>` `.TI2/` then the application data (Table B-3) then a CRC. The application data is exactly 8
  characters: **avionics indicator** (3 digits = preferred characters per display line, `000` for no limit), **ICAO airport**
  (4), **arrival/departure indicator** (1): `A` arrival ATIS, `D` departure ATIS, `C` arrival ATIS with automatic update, `E` en-route
  service / VOLMET, `T` terminate automatic update. A 4-hex-digit CRC follows (character-oriented form, S1 2.2.2.2).
- Decoded: `024KSEAA3BC3` = line width 24, KSEA, arrival ATIS, CRC `3BC3`. Community samples in S4 (`B9/forward-slash.md`):
  `024KRDUAB5F0`, `024KLAXCB1E3`, `024KDFWA81CC`, `040KSFODB6BD`.
- **CRC check:** same CRC as gap 7, computed over `TI2/` + the 8 data characters (`TI2/024KSEAA` -> `3BC3`). It matches our sample and
  all four community samples (`B5F0`, `B1E3`, `81CC`, `B6BD`). The spec's ground-to-ground form is simply `080KPITA`.
- The reply is an `A9` uplink (S2 4.5.9, ARINC 623 B-5): airport ATIS text. Lengthy ATIS uplinks have lower delivery success (S2 4.5.9).

**Test:** every `B9` block should show a station and an airport as separate fields (they need not be equal: live samples have a ground-system station such as `ATSUAXA` and the destination airport `KSFO`). The Alaska `0608` after the flight id in the header
is the message sequence; it is not part of this format.

**Name fields safely:** all four (line width, airport, request type with the five meanings, CRC ok). Plain-English output: "ATIS
request for KSEA, arrival, 24 characters per line".

## Gap 9b: `5Z`

**Documented (S2 5.3.7, high).** "Airline designated downlink": transmitted when entered crew text cannot be appended to a
departure/arrival/ETA report. Layout: originator, sequence, block id, 6-char flight id, then free text up to 220 characters.
The content is the airline's own. S4's `5Z/forward-slash.md` lists command-like variants (`/C3`, `/C6`, `/ET`, `/R3`, `/W1`...) with
guessed meanings (gate request, ground power, METAR by city); medium-to-low confidence, no airline named, single-sourced.
**Safe:** call it "Airline-designated downlink (crew text)" and show the text. Name nothing inside.

---

## Gap 6: `H1 #CFB` central fault display (`FDE0633591029ABZ 06315707301 0 0 ...`)

**Documented.** ARINC 620 Table 4.4-1C lists sub-label `CF` = CMC / central fault display; message mnemonics `CFD` (with header)
and `CFX` (no header) on H1/CF (S2 table of message types). That agrees with our interpreter. The text layout of a fault
report is defined by the avionics maker (Boeing CMC, Airbus CFDIU), not by ARINC 620; S2 only defines the wrapper, the uplink
request and the multi-block handling (S2 5.4.2.3-5.4.2.4: "CFDIU, DFDAU, FMC or cabin terminal messages").

**Community (S4 `H1/CFB.md`, low confidence).** The page names `#CFB` "Crew Flight Bag". That conflicts with ARINC 620 (`CF` =
central fault display; the `B` is the reserved byte, see Corrections). The same page does give a useful acronym list that is
terminology from Boeing/Airbus fault reporting, not a format: FDE (flight deck effect), FR (recorded fault), FLR (real-time failure),
WRN (warning), MPF (maintenance planning function), ACMF (airplane condition monitoring function). `FDE` at the start of your
sample is therefore very plausibly "flight deck effect", i.e. the report is a list of cockpit alerts with time codes.

**Hypotheses for the sample, none verifiable from documents.**
- `FDE0633591029ABZ`: `0633` UTC time of the report (matches your table: "report time"), `1029` = date MMDD (it is 29 October),
  `ABZ` an equation/tag identifier. `59` unknown. Test: `0633`/`1029` should equal the receive time and date on every message.
- Entries like `06315707301`: a 4-digit time (`0631`) followed by 7 digits. If the first 5 digits are an ATA chapter-section-subject style
  code, they should repeat across flights of one type and cluster in ATA chapters 21-80. Cannot be named without Boeing's FIM.

**Test against live.** Collect every distinct 5-digit code with its count; check the same code appears on different tails of the same
type, and whether codes with a shared leading two digits match one system (e.g. packs, hydraulics). If a pattern holds, describe it
"code 06315 (as received)"; do not translate to words.
**Safe:** sub-label identity, report time, date. **As received:** every code.

---

## Gaps 1-3: Southwest `#DFB`, Alaska `#DFBTRP`, United `#DFBA86BBSM25...`

**Documented: nothing public at format level.** S2 only says `#DF` = DFDAU (digital flight data acquisition unit; also called DMU)
and that its output is user-defined (S2 Table 4.4-1C, 5.4.2.3). S4's `H1/DFB.md` is an empty template whose only text is a
title "Digital Flight Bag"; I treat that title as wrong or at best a guess (see Corrections). Searches for ACARS "ACMF/ACMS report
formats" returned only generic material (SKYbrary ACMS page; Lufthansa Technik/Aviatar and Teledyne press releases saying 737 data goes down
via ACARS from the CMC/ACMS). Report layouts are configured per airline in the ACMF; they are private to the operator and the
avionics supplier (Boeing, Teledyne, Honeywell, Collins). No standard assigns them (ARINC 717/ARINC 624 define parameters and the
CFDS, not these text rows).

**Physics-derived hypotheses** (the only legitimate way to name columns from samples alone; none is a documentary fact).

Southwest sample row `91513,32973,-51.8,241,076,CL,00000,0,  N4527.7,W1212...`:

| Field | Value | Hypothesis | Check |
|---|---|---|---|
| 1 | `91513` | time hhmmss without leading zero (09:15:13), or hmmss | must equal receive time within seconds; also compare to the `13.30.19` row style in the same report |
| 2 | `32973` | pressure altitude ft (same as the verified field 2 of the other layout) | at 32,973 ft ISA is about -50.3 C |
| 3 | `-51.8` | static air temperature in C | -51.8 is 1.5 C below ISA there: plausible. In the first layout `04172,...,-3.37` the same slot cannot be SAT (ISA at 4,172 ft is +6.7 C) so that column is something else (candidate: pitch, vertical acceleration, or a TAT difference) |
| 4-5 | `241`, `076` | wind direction (deg) and speed (kt) | wind speed must match groundspeed minus TAS, both from the flight track (adsb.lol) within a few kt |
| 6 | `CL` | flight phase (`CL` climb; expect `CR`, `DE`, `TO`, `AP`) | phase should follow altitude trend; sequence on a single flight |
| 7-8 | `00000`, `0` | unknown | constant? gate on a table of many samples |
| 9-10 | `N4527.7`, `W1212...` | position, degrees + decimal minutes | verify against ADS-B track at the same time (we already used this to settle `#M1BPOS` in INTERPRETER.md) |

For the first layout (`13.30.19,04172,217.2,.354,-3.37`) fields 1-4 are already verified. Hypotheses for 5+: vertical acceleration
(g) or pitch; test by correlating with altitude change between consecutive rows in the same report.

Long numeric rows (`9156 8546 8913 9697  263800 00 8019 8299 8029 8268`): four-digit groups are commonly 4-digit packed numbers
(for example N1/EGT/fuel flow in tenths, or voltages) in an engine-snapshot table, but no document describes this one. **Do not name.**
Test: if a report has two such rows (left/right engine) the groups should pair up in a stable ratio across consecutive reports; a
slowly-varying column across minutes at cruise supports "engine parameter", a column that jumps with phase supports "thrust".

Alaska `#DFBTRP 151106  44.3698 -121.6679 380 131.8 439   1908 0.0...`: `151106` day-hour-minute or hhmmss (15:11:06 vs receive
time decides), decimal degrees latitude/longitude (already verified in INTERPRETER.md), `380` flight level (FL380; hypothesis:
the level field is already named "altitude"), `131.8` and `439`: `439` is plausibly groundspeed or TAS in kt, `131.8` could be gross
weight in thousands of pounds? (a 737-900 at ~131 klb is plausible) or fuel on board; **both unconfirmed**. Test: compare `439`
against ADS-B groundspeed at the same time; compare `131.8` against the same tail across a flight (weight falls with fuel burn: a
steady decrease of order 1 klb per 15 min means weight/fuel).

United `#DFBA86BBSM25-H200-0003 N37324 291015 0925 001812 ...`: shape: tail-or-header token `A86BB` (ignored), `SM25` looks like a
data-set/report id, `-H200-0003` a sequence, `N37324` hemisphere + degrees+minutes (37 deg 32.4 min N), `291015` date ddmmyy,
`0925` time. No document confirms; the position/time pair is verifiable and the rest should stay as received.

**Name fields safely:** (already done) time, pressure altitude, CAS, Mach, position, date and time. After the checks above, add one
column at a time only if its correlation with an external track is within tolerance across at least ten reports on three tails.

---

## Gaps 4-5: SkyWest label 32/17 and Horizon (QX) label 33

**Documented.**
- S4 `32.md`: used by SkyWest (OO). Comma separated; field 3 date/time UTC, field 6 position, field 7 an altitude in feet
  (marked uncertain). Example flight OO5484 2022-06-18. Other fields "unknown". Confidence medium for the position and time, low for
  the rest. This agrees with INTERPRETER.md.
- S4 `17.md`: used by Spirit (NK) and SkyWest (OO). Observed content: METAR requests (UTC time, number, `METAR`, airport) and
  flight-status `KMCOKAUS0303` = departure ICAO, arrival ICAO, ETA hhmm (the page says consistent with arrival). That matches your
  "label 17 uses the label 32 layout" commit only partly: S4 shows a different, shorter layout for label 17; the two may be variants.
  Check the raw samples.
- S4 `33.md`: the page is an empty template with one example from another carrier (`33,F,TYS,DCA,...`). **No public format for
  Horizon's `M30AQX245227,B,29OCT26,133601,+44.219,-122.470,30006,7808,KMFR,KSEA,`.**
- ARINC 620 does not define labels 32, 33 or 37 (S2 label table); they fall in the range airlines use for private formats.

**Hypotheses.** Label 33: `27`/`245227` is part of the message id (`M30A` + flight `QX2452` + `27`): the digits after the flight number look like
the leading part of the downlink sequence, but your header split already handles that. `B` could be a report type or a block/"report
reason" letter; the sample is the second field after the id comma. `7808`: candidate groundspeed? no: at altitude 30006 (probable) it
would be 780.8 kt, too fast; candidate is a fuel/weight figure or a time-to-go in minutes of a different unit; unknown. Test: log the field
across the whole flight; a monotone decrease points to fuel; a bounded value following speed points to speed; a counter points to a
sequence number.
For SkyWest's ten trailing numbers (`14097,249,045,-13,333,368,585,0635,` ) plausible columns by physics:
`14097` altitude ft (confirmed as plausible climb altitude), `249` and `045` speed and heading or wind direction and speed, `-13`
temperature C (ISA at 14,100 ft is -13.0 C: **a near-exact match, so `-13` = static air temperature is a strong hypothesis**),
`333`, `368`, `585` unknown (fuel quantities in hundreds of kg/lb? true airspeed is less than 400), `0635` time hhmm (estimated
arrival or report time). Test: compute ISA at the printed altitude for several samples; if SAT tracks ISA+-10 for all of them, name
`-13` as temperature. Test `0635` against the report time and the flight's scheduled/actual arrival.

**Name fields safely:** date/time, position, airport pair (label 33). Add temperature only after the ISA test passes.

---

## Gap 8: Southwest label 37 that passes the CRC but "looks scrambled"

**Documented.** S4 `37.md`: label 37 is used by Southwest (WN), Republic (YX), Delta/Northwest, Spirit, Endeavor, with four
variants; the WN messages are described as "appears to be obfuscated", with no decode. **No standard describes an encryption or
compression layer for label 37**, and ARINC 620 does not assign it; ARINC 618 carries any printable text in the block. Our sample
(`M64AWN3763054PRCZ  NRKIANRI3A3VEEB,ZZATZDZB (,A GEFFADEFAE(CAB...`) uses only a small alphabet of capitals, digits, commas and
parentheses with repeated short sequences, which is the signature of a substitution or a table-driven compression (small symbol set, many
repeats), not ARINC 622 hex (that would be only `0-9A-F`) and not base64.

**Hypotheses (all low confidence).** (1) Airline private compression with a custom symbol table. (2) A simple substitution
cipher. (3) A hex-like packed encoding over a custom alphabet. **None is documented.** The honest label is "airline-private
encoded text".

**Test against live (quick, no documents needed).**
1. Character histogram over many WN label-37 blocks: ~20-40 distinct symbols suggests a substitution; near-uniform over printable
   suggests compression/encryption.
2. Look for a constant plaintext: the same fixed header (`054PRCZ` here, flight and sequence) repeated at the start of each
   message tells us how much is envelope. Blocks from the same flight a few minutes apart should differ only in a few characters if
   the payload is a table of slowly changing values.
3. Compare with the first-pass numeric content known from H1 `#DFB`: if a label 37 report and a `#DFB` report are sent about the
   same time for the same flight, align them.
Do **not** attempt to break anything that would be an encryption scheme: the owner's rule (clean room, facts only) and the legal
position of decoding an operator's private channel both argue for leaving it as received.
**Safe:** "Southwest label 37, airline-private encoded, CRC ok". Nothing else.

---

## What is safe to name now, and what is not

| Safe now | Needs the checks above first | Stays "as received" |
|---|---|---|
| ARINC 622 ground address, IMI, registration, CRC ok | `-13`-style temperature slots (ISA test) | `AT1` payload hex until DO-258/ED-100 is licensed |
| B9 `.TI2/` fields: width, airport, request type | `439`/`131.8` in `#DFBTRP` (ADS-B and burn trend) | all `#CF` fault codes |
| AFN `FN_CON/FN_AK/FN_CAD` text | Southwest column 4-5 as wind | label 37 payload |
| `5Z` = airline-designated crew text | phase code `CL` etc. | long numeric DFB rows, SkyWest `333,368,585` |
| `CF` = central fault display sub-label | `0633`/`1029` in `FDE...` | |

## Corrections to our own notes

1. **`#DFB` and `#CFB`.** Keep the interpretation in `INTERPRETER.md`: sub-label `DF`/`CF` plus a reserved `B`. The community
   expansion "Digital Flight Bag"/"Crew Flight Bag" is not supported by ARINC 620 (S2 table 4.4-1C: CMC `CF`, DMU `DF`). The label
   in the UI should not say "bag".
2. **`.TI2/` is not Alaska-specific.** It is the standard D-ATIS version 2 request (ARINC 623 Appendix B). The earlier "`B9`/`BA`
   Alaska/Hawaiian message" framing is wrong for `B9`; `BA` is ATCComm (gap 7).
3. **`BA` is two different things.** As an H1 `/BA <addr>.AT1.<reg>...` message it is the ATCComm downlink (ARINC 622). A bare
   `BA` label elsewhere should be treated as the same until a counter-example is found.

## Prioritised: the three decoders most worth building next

1. **ARINC 622 envelope parser with CRC check (gap 7).** Documented, verified on the live sample, covers FANS 1/A traffic from
   every airline (the `AT1`, `CR1`, `CC1`, `DR1`, `ADS`, `DIS`, `AFN`, `FN_*` messages) and gives an integrity check that
   eliminates false parses. Output: ground address, IMI, tail, data length, "CRC ok". AFN text can be fully decoded.
   The CRC is a 15-line function with 3 published test vectors from the spec plus our live vector.
2. **B9/`TI2` ATIS request, plus the `A9` ATIS report uplink (gap 9a).** Documented, verified 5/5 including CRC, and trivial. It is
   a human-readable "ATIS request KSEA arrival", and A9 replies are plain text.
3. **Verified-by-physics temperature/phase columns in the SkyWest label 32/17 and Southwest `#DFB` rows (gaps 1, 4).** Not
   documented, but the ISA test is cheap and objective (SAT should be within 10 C of ISA at the printed altitude; `-13` at 14,097 ft is
   within 0.1 C). Do this as a measured hypothesis with a dated test log, not as a fact, and name a field only after ten reports on three
   tails agree.

Not recommended: label 37 (no documentation, possible private cipher), `#CF` fault codes (need Boeing FIM/maintenance data),
and the CPDLC element table (commercial standards; wait for a licence).

## Open questions for the owner

- Do we have, or will we buy, ARINC 622-5 / 623 and RTCA DO-258A or EUROCAE ED-100A? That decides whether the `AT1` payload can be
  decoded beyond "CRC ok".
- Is an aviation-data source (adsb.lol history, OpenSky) allowed in tests? Several of the above checks (wind, groundspeed, weight
  trend) need an external track.
