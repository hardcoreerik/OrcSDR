# Offline ACARS log analysis

A Python 3 standard-library tool for SD-card JSONL logs. It reads the authoritative `x` hex bytes, retains all original fields and shows unrecognized content as received. It does not connect to the radio or use the network. No web enrichment is implemented.

From the repository root:

```powershell
python -I tools/acars_log/acars_log.py ingest data/acars-sd/20261029.jsonl --db acars.sqlite
python -I tools/acars_log/acars_log.py ingest data/acars-sd --db acars.sqlite
python -I tools/acars_log/acars_log.py report day 2026-10-29 --db acars.sqlite
python -I tools/acars_log/acars_log.py report flight WN0436 --date 2026-10-29 --db acars.sqlite --out flight.md
python -I tools/acars_log/acars_log.py report aircraft N926WN --db acars.sqlite
python -I tools/acars_log/acars_log.py export csv --db acars.sqlite --out positions.csv
python -I tools/acars_log/test_acars_log.py
```

`--db` defaults to `acars.sqlite` for reports and export; it is required for ingest. A directory ingests its immediate `.jsonl` files. An invalid line stops the transaction and identifies its file and line. Re-ingestion deduplicates by UTC, uptime, frequency and normalized text hex. This identity can collapse distinct captures that happen to share all four values. Unknown UTC is stored as zero, included in aircraft and flight reports, and grouped in an `undated` flight; it is excluded from calendar-day reports. Flight grouping uses the header flight ID, tail, and UTC receive date, not an independently verified flight identity; overnight flights span dates, and changing aircraft can produce multiple groups. `AV` addresses are not counted as aircraft.

## Database

- `messages`: original schema fields `v`, `utc`, `up`, `f`, `tail`, `flt`, `lbl`, `mode`, `ack`, `blk`, `end`, `cor`, `fix`, `raw`, `n`, `x`, `p`; decoded `text`, `format`, `summary`, and JSON `details`.
- `aircraft`: tail, first and last valid receive times and count.
- `flights`: flight ID, tail, receive date, first and last valid receive times, count, and route when recognized.
- `positions`: message ID, receive UTC, latitude and longitude, altitude in feet, speed in knots, Mach and format source. Probable altitude is explicitly marked in the source.
- `events`: message ID, kind and JSON details for positions, fault displays, ATIS requests and ATC data links.

Times in position records are receive times, not necessarily the time printed in the report. DFB sample altitude, calibrated speed and Mach appear in flight profiles but do not create positions without coordinates. No multi-block reassembly is performed.

## Decoding limits

Recognized layouts include Southwest DFB headers, sample and climb rows, decimal-degree DFB cruise fixes and TRP, FMC M1BPOS, matching label 32/17 and label 33 position layouts, fault-display lists, CRC-checked ARINC 622 and TI2 ATIS requests. DFB climb and label 33 altitude are probable, not confirmed. Fault codes and ATC payloads remain as received. Other label 17 layouts and unmatched or malformed messages are unclassified; labels are retained. The parser does not interpret every format described by the firmware notes. Raw bytes remain available as hex even when displayed text contains non-printable characters. No external aircraft or flight-history cross-check has been made.

The included reports were generated from the provided 2026-10-29 capture: [WN0436 flight](flight-example.md) and [day summary](day-example.md). They reflect only received messages and do not establish a complete flight history.
