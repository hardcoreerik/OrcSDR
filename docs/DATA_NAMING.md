# OrcSDR data naming conventions

Canonical naming for catalog packs, runtime indexes, on-device paths, source
ledger ids, and staging artifacts. Derive new names from this document; do not
invent a parallel system. See also [DATA_CATALOG.md](DATA_CATALOG.md) and
[DATA_SOURCE_LEDGER.md](DATA_SOURCE_LEDGER.md).

**Listen-to-ATC / ATC airband uses catalog pack id `faa_aviation`** (magic
`ORCCAT1`, destination `/orcsdr/data/faa_aviation.idx`). Do not create a second
ATC pack id.

## 1. Catalog pack id

Stable snake_case `{scope}_{domain}` registered in
`tools/data_catalog/build_catalog.py` (`PACK_IDS`) and firmware
(`catalog_sync.cpp` built-in ids).

| Rule | Detail |
| --- | --- |
| Form | `snake_case`, e.g. `faa_aviation`, `fcc_broadcast` |
| Stability | Never rename once firmware or a signed catalog references it |
| ATC airband | **`faa_aviation`** only |
| P25 extras | `p25_*` with the bounded id rules in `is_p25_pack` / firmware |

Current fixed ids: `faa_aircraft`, `faa_aviation`, `noaa_weather`,
`fcc_broadcast`, `lane_county_map`, `international_broadcast`, `hf_schedules`
(+ optional `p25_*`).

## 2. Runtime magic / schema

Binary or text indexes start with `ORC` + short domain token + version digit.
Most schemes are **newline-terminated** (`ORCCAT1\n`, `ORCBRD1\n`, `ORCMAP1\n`).
`ORCADSB1` is the known eight-byte prefix **without** a trailing newline in the
validator (`prefix != b"ORCADSB1"`).

| Magic | Packs / use |
| --- | --- |
| `ORCADSB1` | `faa_aircraft` |
| `ORCCAT1\n` | `faa_aviation`, `noaa_weather` (default catalog record indexes); ATC rows also use this |
| `ORCBRD1\n` | `fcc_broadcast`, `international_broadcast`, `hf_schedules` |
| `ORCMAP1\n` | `lane_county_map` |
| (profile text) | `p25_*` version-2 `profile.cfg` (not an `ORC*` magic) |

Firmware cross-check: `atc_presets` expects line `ORCCAT1`; broadcast DB expects
`ORCBRD1`; ADS-B expects `ORCADSB1`; offline map expects `ORCMAP1`.

## 3. On-device path

**Preferred:** `/orcsdr/data/<pack_id>.idx`

| Pack id | Destination | Notes |
| --- | --- | --- |
| `faa_aviation` | `/orcsdr/data/faa_aviation.idx` | Firmware `atc_presets.hpp` `kRuntimePath` |
| `noaa_weather` | `/orcsdr/data/noaa_weather.idx` | Preferred pattern |
| `fcc_broadcast` | `/orcsdr/data/fcc_broadcast.idx` | |
| `international_broadcast` | `/orcsdr/data/international_broadcast.idx` | |
| `hf_schedules` | `/orcsdr/data/hf_schedules.idx` | |
| `lane_county_map` | `/orcsdr/data/lane_county_map.idx` | |
| `faa_aircraft` | `/orcsdr/data/adsb_aircraft.idx` | **Known exception:** filename is `adsb_aircraft.idx`, not `faa_aircraft.idx` (legacy ADS-B path; also legacy `/orcsdr/adsb_aircraft.idx`) |
| `p25_*` | `/orcsdr/p25/<pack_id>/profile.cfg` | Separate tree |

Archive destinations typically mirror under `/orcsdr/data/<pack_id>_source.zip`
(or pack-specific archive names in `catalog-input.example.json`).

## 4. Source ledger id

Snake `{publisher}_{product}` in `tools/data_catalog/broadcast-sources.json`
(and analogous ledger rows for non-broadcast packs).

Examples: `fcc_lms`, `ised_sms_broadcast`, `ncc_am_fm_stations`,
`ofcom_txparams`.

| Rule | Detail |
| --- | --- |
| Gate | Only `release_allowed: true` sources may appear in a signed pack's `source_ids` |
| Aviation NASR FRQ | **`faa_nasr_frq`** — matches `SOURCE` in `normalize_faa_nasr_frq.py` |
| Record ids | Staging rows may use `{source}:{facility}:{freq}:{tag}` (see normalizer) |

`build_catalog.py` rejects broadcast packs that reference unknown or
non-`release_allowed` ledger ids.

## 5. Staging artifact names

Under `artifacts/<domain>-staging/` (e.g. `artifacts/atc-staging/`).

| Kind | Pattern | Example |
| --- | --- | --- |
| Master / slices | kebab descriptive | `faa-nasr-frq-airband.ndjson`, `faa-nasr-frq-reviewed.csv`, `faa-nasr-frq-nearest24-eug.csv` |
| Receipts | `*.receipt.json` or `*-receipt.json` | `faa-nasr-frq-receipt.json` |
| Local proof indexes | may use pack id + slice tag | `faa_aviation_nearest24_eug.idx` (build-time only; not a new pack id) |

Staging is not a signed catalog release. Do not publish or flash staging
masters as catalog assets without the normal build + rights gate.

## 6. Labels / ORCCAT1 ATC rows

Device ATC presets (`build_faa_aviation_index.py` / `atc_presets`):

| Field | Convention |
| --- | --- |
| Magic line | `ORCCAT1` |
| Row | `ATC <lat_e7> <lon_e7> <frequency_hz> <label>` |
| Label | ASCII printable only, length **≤ 31** (`LABEL_CAPACITY`) |
| `frequency_hz` | integer Hz (airband builder enforces ~118–137 MHz) |
| lat / lon | integer **e7** (degrees × 10⁷, half-up) |

Generic `build_record_index.py` ORCCAT1 JSON record lines are a separate curated
CSV->index path for catalog packs; ATC voice presets use the `ATC …` row form
above.

## 7. Location-sliced packs

- Master staging (e.g. full US airband NDJSON) may be huge.
- Device ORCCAT1 ATC capacity is **24** (`RUNTIME_CAPACITY`).
- A nearest-N / geo slice is a **build-time view** of pack `faa_aviation`.
- Sliced runtime still ships under pack id **`faa_aviation`** and destination
  **`/orcsdr/data/faa_aviation.idx`**.
- **Do not** invent pack ids like `faa_aviation_near`, `faa_aviation_eug`, or
  `atc_near_*`.

## 8. What not to do

- Do **not** rename `faa_aviation` -> `atc_*` (or any other new ATC pack id).
- Do **not** reuse broadcast magic `ORCBRD1` for ATC airband indexes.
- Do **not** put gated / `release_allowed: false` sources in signed
  `source_ids`.
- Do **not** treat staging proof `.idx` files as published catalog assets.
- Do **not** change on-device destinations that firmware already hard-codes
  without a coordinated firmware + catalog migration.

## Current registry

| Pack id | Magic | Destination | Purpose |
| --- | --- | --- | --- |
| `faa_aircraft` | `ORCADSB1` | `/orcsdr/data/adsb_aircraft.idx` | ICAO / registration ADS-B lookup |
| `faa_aviation` | `ORCCAT1` | `/orcsdr/data/faa_aviation.idx` | Airport / ATC airband (Listen-to-ATC) |
| `noaa_weather` | `ORCCAT1` | `/orcsdr/data/noaa_weather.idx` | NWR / SAME weather radio |
| `fcc_broadcast` | `ORCBRD1` | `/orcsdr/data/fcc_broadcast.idx` | US AM/FM station cards |
| `international_broadcast` | `ORCBRD1` | `/orcsdr/data/international_broadcast.idx` | Non-US AM/FM station cards |
| `hf_schedules` | `ORCBRD1` | `/orcsdr/data/hf_schedules.idx` | HF / shortwave schedules |
| `lane_county_map` | `ORCMAP1` | `/orcsdr/data/lane_county_map.idx` | Offline map tiles/index |
| `p25_*` | profile v2 | `/orcsdr/p25/<id>/profile.cfg` | Per-system P25 control profile |

Aviation FRQ source ledger id: **`faa_nasr_frq`**. Broadcast examples:
`fcc_lms`, `ised_sms_broadcast`, `ncc_am_fm_stations`.
