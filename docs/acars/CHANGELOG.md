# ACARS Changelog

This file tracks ACARS dashboard and decoder changes. The repository has no repository-wide CHANGELOG.md.

## 2026-10-29 (later) - Live VDL2 captures and two new formats

## 2026-10-29 (evening) - Review of the overnight run, `#DFB` sample blocks and label 33

- Second pass on the SD log (3,057 blocks, 20:50Z): honest decoded share 56.7% -> 63.1%. Added: flight data unit headers with a route (`KEUGKSMF`, `CYVRKPSP`) and an FMC style position (`N44079W123220`); Alaska `#DFB` TRP blocks that start on a new line; H1 sub-label T1 (`RTE`/`PLF` lines with date, tail, flight and route); in-range reports (labels 28 and 80: day, route, tail, expected arrival). The harness caught two mistakes of mine on the way (the first number of an in-range report is the flight number, not a time; an empty summary on a position-only block). Not flashed.

- Robustness pass over a whole day of real blocks (2,393 from the SD log; harness `tests/acars_corpus_check.cpp`, ASan/UBSan, now part of `tools/test-acars-ui.sh` with a 329 block sample in `tests/data/`):
  - The decoded share was overstated: every flight-data block counted as decoded even when the screen said the layout was not. `decoded` is now true only when text was understood. Honest share of the day: 57%.
  - Fixed: label 32 variant without spaces; D-ATIS station of 4 to 7 characters; fault-display lists with line breaks in the text.
  - Added: label 22 position report (degrees, minutes, seconds) and label 10 `LDR01` position with airports, both with altitude/temperature marked probable.
  - Receiver self-heal: wrong-rate blocks for 3 s with no good block ask for the tune again (the receiver was stuck once overnight and recovered only after reopening ACARS).
  - ARCHIVE store raised from 300 to 2000 blocks (PSRAM), 1 MiB of the log read at boot. Not flashed.
  - United `PNM`/`HYDN` reports (5 s samples with altitude, outside air temperature and Mach) are readable but need blocks joined across a message; not built.

- Later: label 17 uses the label 32 layout; Southwest climb position block (`CL`); ARINC 622 envelope parser with CRC check (`#M1B/BA ...AT1...`); D-ATIS request decoder for label B9 (`.TI2/`). Basis: `research/FORMAT_DOCS_RESEARCH.md`. CRC verified independently on the spec examples and live blocks. 713 messages in the next 1h52m on the new firmware (no gaps, 29 ms per block). Seen but not yet decoded: self-describing engine/air-data lines from GS0001 (`M1B ... 7.25000 mbar T20 (FROM ADC2)`), Alaska `#DFBE329KSMFKSEAN43046W121575...` and `#DFBD3B029KPDXKLASN44543W12201...`.
- Overnight run: unit up 6.75 h, 712 messages counted, 1063 verified frames, no gaps or drops, decoder 28.2 ms average and 143 ms worst per 65 ms block. Only the last 300 blocks (13:31-14:21Z) survive: the SD archive is a 300-block ring and the screen holds 48, so the earlier part of the night is lost (counters only).
- Interpreter: Southwest `#DFB` header block (type, date, flight, route) and sample blocks (time, altitude, speed, Mach), and label 33 position reports. Host tests pass under ASan/UBSan.
- Cross-check with OpenSky (authenticated, credentials kept outside the repository): WN0436 N926WN left KEUG (OpenSky firstSeen 13:28:28Z); the `M1BPRG` report at 13:35:06Z, N44 06.0 W122 36.2, is 0.2 nm from OpenSky's 13:35:06Z position (44.1011, -122.5991). Our `#DFB` altitudes run ahead of OpenSky's coarsely sampled track by about 15 s of climb (about 800 ft). QX2452 (label 33) could not be matched: OpenSky had not yet listed that flight.
- First live VDL2 traffic on the Tab5 (SkyWest N244SY on OO137C, Delta N338DU on DL0661): 18 CRC-valid AVLC frames, 10 ACARS, no drops.
- Interpreter: label 32 position reports (date, time, position; altitude marked probable; other values as received) and `H1 #CFB FDE`
  flight-deck-effect lists (report time and date, entries as received). Captures, screenshots and the cross-check against FAA,
  OpenSky and adsb.lol are in `captures/2026-10-29-vdl2-136975/`.
- `tools/acars_adsb_xref.py` compares a decoded position with the live adsb.lol position.
- Decoder CPU on the Tab5 is about 28 ms per 65 ms block when idle (44% of a core). Likely cause: double-precision arithmetic in
  `vdl2_receiver.cpp` (the P4 has a single-precision FPU only). Not changed yet.
- Quiet-hour run: 30 minutes on 136.975 MHz from 07:03Z with no messages; light traffic.
- Device tests on COM17 (2026-10-29, 08:15Z): `RTL_ACARS SELFTEST` 10 of 10 PASS; `RTL_ACARS ARCHIVETEST` PASS (12 written, 12 loaded, none dropped).
- One continuous 45-minute run on 136.975 MHz, 08:16:32 to 09:01:22Z, single watcher, 135 status samples: no crash, no stream gaps,
  no dropped messages, decoder 27.98-28.12 ms per 65 ms block, worst block 81 ms, internal RAM free at least 50.9 KB. Counters moved from
  12 to 14 valid headers and 10 to 11 CRC-valid AVLC frames (one more frame with no data); no new ACARS message arrived. The 08:14Z
  N395CM / ATN3337 capture (matched to adsb.lol and the FAA registry) came just before the run.
- Further capture attempts: a 30 minute channel hunt (136.975 down to 136.750 MHz, 3 minutes each, 09:03-09:33Z) heard nothing; a second continuous 
  45 minute run on 136.975 MHz (09:34:48 to 10:19:37Z, 135 samples) saw valid headers go from 14 to 19 and verified frames from 11 to 12 (one more
  frame with no data), no ACARS message. Nothing from those windows could be matched to a web source.
- Third continuous 45 minute window on the analog channel 131.550 MHz (10:22:39 to about 11:07Z): 44 CRC-verified blocks, all squitters from EUG,
  matched to OurAirports (KEUG at 44.1246N 123.2120W; squitter 0.5 nm away). No aircraft block was heard on that channel that night.
- Fourth 45 minute window (11:12:18 to 11:57:19Z): the unit's own ADS-B receiver (1090 MHz) on the ADS-B screen with serial trace logging, checked
  once a minute against adsb.lol by `tools/adsb_live_xref.py`. The receiver reported RECEIVING / signal good but decoded 0 aircraft and 0 messages
  (VHF whip antenna, night-time traffic), so there was nothing to compare. The serial verbosity was restored to NORMAL afterwards.
- Fifth window, 11:58:08 to 12:43Z on VDL2 136.975 MHz, with traffic: 135 ACARS messages, 213 verified frames, decoder 28.4 ms per 65 ms block.
  Four aircraft (SkyWest N244SY, Southwest N8862Q and N483WN, Envoy N298FR) matched live against adsb.lol at 12:45:28Z (callsigns SKW3708, SWA1701,
  SWA1899, ENY4048) and N244SY's KEUG departure against OpenSky. The label-32 and fault-display parsers were seen on the live screen. New unread
  format: Southwest `H1 #DFB` multi-block data reports. Screenshots and tables in `captures/2026-10-29-vdl2-136975/`.

## 2026-10-29 - VDL Mode 2 on the ACARS dashboard (flashed to COM17, live RF not yet heard)

- **Receive path chosen by channel:** a requested channel from 136.725 to 136.975 MHz runs the native `vdl2::Channel` (250 kS/s stream,
  channel 62.5 kHz above the tuned centre, /5 to 50 kS/s, D8PSK, Reed-Solomon FEC, AVLC) instead of the 240 kS/s analog front end.
  The analog receiver is unchanged. `Hooks::tune` now takes the stream rate, so `main.cpp` got smaller, not larger.
- **Records:** ACARS inside AVLC frames posts like an analog block. Other data frames (ATN, CPDLC, ADS-C) are kept as raw hex under
  the label `AV` with the 28-bit source address as the tail and are shown, not interpreted. Frames with no data are only counted.
- **Channels page:** a `136.975 VDL2` preset in every region; the list now has seven 48 px rows. `RTL_ACARS STATUS` gained `vdl2`,
  `v2_hdr_ok`, `v2_fec_ok`, `v2_acars`, `v2_raw`, `v2_other`.
- **Evidence:**
  - the native decoder passes the xng off-air fixture (`vdl2_offair_6s.i16`, 6 s at 50 kS/s): 4 CRC-valid AVLC frames, including
    ACARS from HB-IJW (label B9, then a label `_?` acknowledgement), under ASan/UBSan;
  - the dashboard adapter passes a synthetic-IQ test (ACARS frame, raw frame, counted frame, switch back to analog);
  - on the Tab5 the 136.975 preset tunes, the stream runs at 250 kS/s with no wrong-rate blocks and no gaps.
- **Measured on the Tab5 (idle channel, 15 blocks/s of 32 KB = 65 ms of signal each):** the decoder takes 22 ms on average per
  block (about a third of one core) with a worst case of 81 ms, which is longer than one block. Internal RAM free 59.9 KB (min 42.6 KB);
  the receiver state lives in PSRAM. USB throughput was not stressed separately.
- **Not verified:** any live VDL2 frame on the device or the 60 s indoor capture from a second dongle (no energy bursts above the noise,
  7% of samples clipped); a dumpvdl2 cross-check (not built: needs an external build); BER/SNR sweep; collision behaviour.

## 2026-10-28 - Application-text interpreter (host-tested, not flashed)

- **New `acars_interpret.{hpp,cpp}`:** plain-language reading of squitters, acknowledgements, autotune, link tests, H1 sub-labels,
  FMC position reports (`#M1BPOS`), label-21 position reports, and flight-data-unit (`#DF`) cruise fixes, `TRP` blocks and route
  headers, plus names for about 50 labels and airline names for the common US prefixes. Details, sources and cross-checks:
  `INTERPRETER.md`.
- **Dashboard:** the message detail page's APPLICATION INTERPRETATION panel shows the decoded headline and lines (it used to always say
  "NOT SUPPLIED"); the LIVE list shows a cyan interpreted summary for decoded blocks and keeps raw text for everything else. No new
  per-record storage.
- **Tests:** `tests/acars_interpret_tests.cpp` uses blocks captured from the Tab5 on 131.550 MHz, with a hostile-input loop under
  ASan/UBSan, and `tests/acars_dashboard_render_tests.cpp` checks list and detail drawing. `tools/test-acars-ui.sh` passes in full.
- Not yet built with ESP-IDF or run on a device.
