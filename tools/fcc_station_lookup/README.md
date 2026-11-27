# FCC station lookup: source audit

`audit.py` streams selected root ULS tables from an owner-supplied ZIP and reports
only aggregate row types and field widths. It reads selected members to EOF so
ZIP CRC checks are exercised, and hashes the original archive. Raw records and
personal information are not printed or extracted. The hash is an integrity
pin, not an independently authenticated publisher checksum.

Run from the repository root:

```powershell
python tools/fcc_station_lookup/audit.py F:\Ai\OrcSDR-data\fcc-database\raw\us\l_LMpriv.zip FR EM
python -m unittest discover -s tests/fcc_station_lookup -v
```

Local run on 2026-11-27: archive 418,485,639 bytes, SHA-256
`0f8ff0428d39468f2691707a2216adca0151499849d1bdfad0e0be1e1d5b89d7`;
FR: 5,726,333 rows of 30 fields; EM: 9,392,561 rows of 16 fields;
no unexpected record types. These are source row counts, not active licenses,
unique stations, eligible DMR assignments or national coverage. The archive
name and record shapes do not establish semantic correctness, provenance
authenticity, currency, license status, relations or redistribution rights.

This is a host-side audit, not a database generator or device lookup service.
The existing FCC research's semantic-admission and source-relationship findings
remain open. No output package, firmware integration or hardware performance is
claimed by this result.

## Private exact-frequency sizing experiment

`frequency_probe.py` builds an experimental sorted dictionary and source-row
postings from one FR table using disk-backed host SQLite. It is deliberately
not an installable or signed package. It does not resolve license status,
emissions, locations, organizations, duplicate authorizations, or intervals;
row numbers are only meaningful against the pinned source archive. Keep the
output private and outside Git. Malformed table structure aborts publication;
invalid frequencies and non-exact ranges are counted and excluded.

```powershell
python tools/fcc_station_lookup/frequency_probe.py F:\Ai\OrcSDR-data\fcc-database\raw\us\l_LMpriv.zip F:\Ai\OrcSDR-data\fcc-database\experiments\l_LMpriv-frequency-probe-20261127.bin
```

For the archive pinned above: 5,726,333 rows = 5,663,236 exact-frequency
postings + 61,919 non-exact intervals + 1,178 invalid or unsupported values.
The result contains 19,025 unique frequencies; the largest posting list has
25,141 rows. The output is 22,957,360 bytes and has SHA-256
`df02f920e49d179f0391a78496c1de7ebf7e333541f5ed434f55c4bf474feb14`.
An independent sequential directory check confirmed ordering, contiguous
posting offsets, counts, and file size. These are unfiltered source rows, not
DMR candidates or a national coverage or performance claim. At this size,
the frequency-only result leaves little room under the preferred 25 MiB target;
the three requested eligibility scopes must be measured before design choices.
