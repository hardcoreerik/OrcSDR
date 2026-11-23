# Brief: offline ACARS log analysis tool (PC side)

Owner goal: use the ACARS data the Tab5 logs on its SD card, classified and filtered over time, to generate reports about
individual aircraft and flights. Build the offline PC tool. You are working in this worktree; another session (Claude) is editing
firmware at the same time.

## Hard rules

- Touch ONLY `tools/acars_log/**` and `docs/acars/log-analysis/**`. Do not edit `apps/`, `tests/`, `docs/acars/*.md`, or anything
  else. Do not commit, push, merge or switch branches: leave your work uncommitted and say what you changed at the end.
- Python 3 standard library only (sqlite3, json, argparse, unittest, csv, datetime). No pip installs. No network access in the
  default code path.
- Do not touch COM17 or any serial port. The device is in use by another session.
- Clean-room: do not copy code or tables from dumpvdl2, vdlm2dec, acarsdec, libacars or acarshub. The format facts you need are in
  `docs/acars/INTERPRETER.md` and `docs/acars/research/FORMAT_DOCS_RESEARCH.md` (our own notes; read them first). Our own C++ in
  `apps/orcsdr-tab5/ui/acars_interpret.cpp` may be read as a specification of what is already verified.
- Never write credentials into any file. Optional web enrichment (below) must read tokens from environment variables only.
- Honest output: when a field is not understood, report it "as received". Never name a field that is not named in the notes.

## Input

`data/acars-sd/20261029.jsonl` (untracked sample, 2165 lines, copied from the device SD card `/orcsdr/acars/20261029.jsonl`; more
files named `YYYYMMDD.jsonl` will follow, and `undated.jsonl` for blocks logged before the clock was valid). One JSON object per line:

| key | meaning |
|---|---|
| `v` | schema version (1) |
| `utc` | receive time, Unix seconds UTC (0 or absent = clock not valid) |
| `up` | device uptime in ms |
| `f` | channel frequency in Hz (136975000 = VDL Mode 2 common channel; 131550000 = analog ACARS) |
| `tail` | aircraft registration (empty for squitters); for VDL2 non-ACARS frames the 28-bit source address in hex |
| `flt` | flight id as in the ACARS header, e.g. `WN0436` |
| `lbl` | two character ACARS label; `AV` = a VDL2 data frame that is not ACARS |
| `mode` | `2` = ACARS mode 2 |
| `ack`, `blk`, `end` | ACARS acknowledgement character, block id, end-of-text type |
| `cor`, `fix` | error-correction counters |
| `raw`, `n` | raw length and text length |
| `x` | the exact block text as hex (authoritative) |
| `p` | printable preview of the text (greppable; non printable bytes replaced) |

The text begins with a message header: `M`/`F`/`D`/... sequence letters + block id, then the flight id. Examples, all real:

```
D15AWN0436#DFB05201,0926,B737-700,261029,WN0436,KEUG,KDEN,2341,...      Southwest flight data unit header block
D15EWN0436#DFB00,00000000,00000000  13.30.19,04172,217.2,.354,-3.37,...  Southwest sample row: time, altitude ft, CAS kt, Mach (verified)
F06AWN0436#M1B/BA USADCXA.AT1.N926WN6082D826032E1F                         ARINC 622 ATC data link, CRC checkable
M85AOO3736/KSJC.TI2/024KSJCC51D2                                           D-ATIS request, CRC checkable
M89AOO3736P,E,2026-10-29 16:31:59,6650,68434,N 43.789 W 122.922,3712...    SkyWest label 32 position
M30AQX245227,B,29OCT26,133601,+44.219,-122.470,30006,7808,KMFR,KSEA,      Horizon label 33 position
F32AAS1337#M1BPOSN44196W121312,CHISM,163308,285,PUH01,163725,...          FMC position report
```

## What to build, in this order

1. `tools/acars_log/acars_log.py` with sub-commands (argparse):
   - `ingest <files or dirs...> --db acars.sqlite`: idempotent (unique on utc, up, frequency, text hex). Tables: `messages`
     (all keys plus decoded text), `aircraft` (tail, first/last seen, count), `flights` (flight id + tail + date: first/last seen,
     message count, origin/destination when known), `positions` (message id, utc, lat, lon, altitude ft, speed kt, mach, source
     format name), `events` (message id, kind, key/value details: atis request, atc link, fault display, position).
   - `report aircraft <tail>` and `report flight <flight-id> [--date YYYY-MM-DD]`: Markdown timeline to stdout (and `--out file`):
     times, labels, what each message says in plain language, route, altitude/speed profile, positions as a table, ATC data link
     contacts, ATIS requests, counts by label.
   - `report day <YYYY-MM-DD>`: fleet summary: messages per hour and channel, number of aircraft and flights, busiest flights, labels
     histogram, unclassified/undecoded share (and the top undecoded formats with one example each).
   - `export csv` for positions.
2. `tools/acars_log/classify.py`: classify each block by format name using ONLY patterns documented in INTERPRETER.md and the
   research file. Implement parsers for the verified formats (position fixes, Southwest DFB header/sample/climb rows, ARINC 622 with
   CRC-16 poly 0x1021 start 0xFFFF final complement over IMI + "." + registration + data bytes, B9 TI2 with the same CRC over
   "TI2/" + eight data characters, label 32/17/33, M1BPOS, fault display lists). Everything else is `unclassified` with its label.
3. `tools/acars_log/test_acars_log.py` (unittest): at least one real line per format (take them from the sample file), CRC test
   vectors (`AT1.N678DL` + `4104532300` -> `A9B3`; `ADS.N678DL` + `032D` -> `2633`; `TI2/024KSEAA` -> `3BC3`), idempotent ingest, report smoke test.
4. Optional, behind `--enrich`: adsb.lol (`https://api.adsb.lol/v2/reg/<TAIL>`) for current position and ICAO hex, and OpenSky
   flight history using OAuth client credentials read from the environment variables `OPENSKY_CLIENT_ID` and `OPENSKY_CLIENT_SECRET`.
   Default off. Cache nothing that contains credentials. Cross-check output must say which web source and when.
5. `docs/acars/log-analysis/README.md`: how to run it, the schema, what is and is not decoded, and one real example report generated
   from the sample (a flight with many messages, e.g. WN0436 / N926WN, and the day summary).

## Done means

`python -I tools/acars_log/test_acars_log.py` passes; `ingest` of the sample file followed by `report day 2026-10-29` and
`report flight WN0436` produce sensible Markdown; running `ingest` twice does not duplicate rows. Final message: files created,
how to run, what is decoded vs "as received", and anything you could not verify.
