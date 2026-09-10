# Offline broadcast-station database

`ORCBRD1` is OrcSDR's local AM, FM, and shortwave reference index. It is an
SD-card data asset, not firmware content and not a preset store.

## Record contract

Every record has a stable source-qualified `id`, `source`, `source_id`,
`service` (`am`, `fm`, or `shortwave`), and integer `frequency_hz`. Other
factual fields are retained only when supplied by the source: callsign, name,
location, coordinates, power, status, RDS PS/PI, and shortwave schedule,
language, transmitter, target, mode, and season.

The index contains a sorted `{frequency_hz, record_offset}` table followed by
length-prefixed canonical JSON records. A receiver therefore reads only the
candidates for a tuned frequency. A frequency/schedule match is reference
information, never proof that the RF signal has been identified.

## Build a local index

```powershell
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\tools\data_catalog\broadcast-stations.example.ndjson `
  --out .\artifacts\broadcast-stations.idx `
  --source-date 2026-09-10
```

The builder rejects malformed source rows by default and reports the record
counts, source/country/service coverage, bytes, and SHA-256. Validate an
existing local file with `inspect` before it is staged for a signed catalog
pack.

The local staging set currently includes ISED Canada's 2026-09-02 broadcast
release: 4,286 factual records (512 AM, 3,774 FM), all explicitly marked
`CA`. Its raw ZIP, SHA-256 receipt, normalized NDJSON, and `ORCBRD1` index are
kept together under `artifacts/broadcast-staging/`; it is a Canadian slice,
not a claim of worldwide coverage.

```powershell
python .\tools\data_catalog\fetch_broadcast_source.py --source ised_sms_broadcast `
  --out .\artifacts\broadcast-staging\ised-broadcast.zip
python .\tools\data_catalog\normalize_ised_broadcast.py `
  --input .\artifacts\broadcast-staging\ised-broadcast.zip `
  --out .\artifacts\broadcast-staging\ised-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\ised-broadcast.ndjson `
  --out .\artifacts\broadcast-staging\international_broadcast.idx `
  --source-date 2026-09-02
```

## Dual-track release policy (Erik-approved)

1. **SHIP track** - only sources with clear redistribution terms in
   `broadcast-sources.json` (`release_allowed: true`) may enter a signed
   catalog / merged international index after review.
2. **GATED stage track** - other official lists may be staged locally with
   `release_allowed: false` for rights review and tooling; never claim an open
   licence; never include those `source_id`s in a signed catalog pack.

NRTA stays gated (`release_allowed: false`). OFCA Hong Kong analogue sound
frequencies are SHIP-track (`ofca_sound_freq_table`, country `HK`). Taiwan NCC
AM/FM open data is SHIP-track (`ncc_am_fm_stations`, country `TW`, OGDL v1).

## Source gate

`tools/data_catalog/broadcast-sources.json` is the release gate and the
manual-source directory. It presently approves regulator data for the US,
Australia, United Kingdom, Canada, Hong Kong (OFCA), and Taiwan (NCC). It explicitly excludes Japan MIC,
China NRTA, HFCC, and EiBi until their individual reuse terms are recorded. A global pack
must publish its exact countries, source dates, exclusions, and attribution.

Fetch an approved direct source and capture its SHA-256 receipt:

```powershell
python .\tools\data_catalog\fetch_broadcast_source.py --source acma_broadcast_transmitters `
  --out F:\data\acma-broadcast.zip
```

If a regulator supplies only a download page, retrieve the current file there,
retain the original archive and URL, then record its SHA-256 and transformation
version before building an index:

```powershell
python .\tools\data_catalog\fetch_broadcast_source.py --source fcc_lms `
  --file F:\downloads\Current_LMS_Dump.zip --out F:\data\fcc-lms.zip
```

Do not ship a source marked `release_allowed: false`, even if it is publicly
reachable.

## FCC LMS (United States AM/FM)

The FCC Media Bureau publishes the Licensing and Management System (LMS)
public database on the [FCC Open Data portal](https://opendata.fcc.gov/Media/LMS-Public-Database-Files/nsck-y87u).
The Socrata dataset metadata records `licenseId: USGOV_WORKS`, name
"Public Domain U.S. Government", terms at <https://www.usa.gov/government-works>,
attribution "FCC Media Bureau" — matching the `broadcast-sources.json`
entry (`release_allowed: true`). The current archive is
`Current_LMS_Dump.zip`, published on the FCC's own
[LMS downloads page](https://enterpriseefiling.fcc.gov/dataentry/public/tv/lmsDatabase.html);
the schema PDF is served from
`https://enterpriseefiling.fcc.gov/dataentry/api/download/lmschema`.

Retrieve `Current_LMS_Dump.zip` in a browser (the FCC endpoint currently
rejects non-interactive HTTPS clients) and pass it to
`fetch_broadcast_source.py --file` so the SHA-256 receipt is produced from
the untouched archive. Then normalize and build the ORCBRD1 index in
`artifacts/broadcast-staging/`:

```powershell
python .\tools\data_catalog\fetch_broadcast_source.py --source fcc_lms `
  --file F:\downloads\Current_LMS_Dump.zip `
  --out .\artifacts\broadcast-staging\fcc-lms.zip
python .\tools\data_catalog\normalize_fcc_lms.py `
  --input .\artifacts\broadcast-staging\fcc-lms.zip `
  --out .\artifacts\broadcast-staging\fcc-lms-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\fcc-lms-broadcast.ndjson `
  --out .\artifacts\broadcast-staging\fcc_broadcast.idx `
  --source-date <YYYY-MM-DD from the FCC snapshot>
python .\tools\data_catalog\build_broadcast_index.py inspect `
  --input .\artifacts\broadcast-staging\fcc_broadcast.idx
```

`normalize_fcc_lms.py` reads only the `FACILITY` table
(`facility.zip → facility.dat`, pipe-delimited with a header row),
filters to `service_code` in {AM, FM}, parses the FCC's kHz-for-AM /
MHz-for-FM `frequency` string into integer Hz, and emits the canonical
ORCBRD1 station-card fields (`id`, `source`, `source_id`, `service`,
`frequency_hz`, plus `callsign`, `name`, `city`, `region`, `country="US"`,
`status` when present). Coordinates and power live in the
`APP_AM_ANTENNA` / `APP_ANTENNA` tables and are omitted here; the record
contract permits optional fields to be absent when the source does not
supply them. A `facility_status` value of `LICEN` is a licensing record,
never proof that the transmitter is currently on air.

The `fcc_broadcast` pack retains **every factual FCC facility status**
from `lkp_facility_status.dat`, including `LICEN` (licensed),
`LICSL` (licensed and silent), `LICSU` (licensed and suspended),
`LICRP` (licensed at reduced power), `LICAN` (license cancelled),
`FVOID` (facility void), `PRCAN` (permit cancelled),
`CPAPP`/`CPIDL`/`CPOFF` (construction-permit lifecycle),
`INTOP`, `PTANF`, `AUCTN`, `EXPER`, and `UNKNO`. It is **not** an
active-stations list — a status is source-preserved provenance, and a
frequency match is a reference result only, never a claim that the
transmitter is currently on the air.

Known limitation — **~181 records with no `region`**: FCC LMS's
`community_served_state` is blank on a small number of rows (Mexican
`XE-` / Canadian `CK-` cross-border coordination facilities and a
handful of legacy placeholders). These rows are retained because
`facility_id` and `frequency` are valid, but they carry `country="US"`
by default and have no `region`. The normalizer deliberately does **not**
invent a state or country mapping for these; treat them as
provenance-only records until an authoritative regulator supplies the
correct jurisdiction.


## Taiwan NCC AM/FM (data.gov.tw 6445 / 6446)

**Status:** SHIP-track. Ledger id `ncc_am_fm_stations`, `country=TW`,
`release_allowed: true`. Licence is 政府資料開放授權條款-第1版
(OGDL-Taiwan-1.0), CC BY 4.0 compatible
(<https://data.gov.tw/license>). Attribute the National Communications
Commission (國家通訊傳播委員會) and the dataset titles.

- FM catalog: <https://data.gov.tw/dataset/6445>
- AM catalog: <https://data.gov.tw/dataset/6446>
- FM download (XLS): `api.ncc.gov.tw/uploaddowndoc?file=datagov/1522493929772027904.xls...`
  (sheet `調頻FM_115_7_3`, as-of ROC 115/7/3 = 2026-07-03)
- AM open-data XLS advertised by dataset 6446
  (`datagov/1490970926492160000.xls`) currently returns
  `欲下載的檔案不存在` on the NCC host. Staging uses the same-day NCC
  portal PDF (`serno=51003_4173_news`) table-extracted to
  `artifacts/broadcast-staging/ncc-am.csv`.

```powershell
python .\tools\data_catalog\fetch_broadcast_source.py --source ncc_am_fm_stations `
  --out .\artifacts\broadcast-staging\ncc-fm.xls
# AM: if the open-data XLS is still missing, download the NCC portal PDF
# from am_download_url in broadcast-sources.json, extract to ncc-am.csv,
# then:
python .\tools\data_catalog\normalize_ncc_broadcast.py `
  --fm .\artifacts\broadcast-staging\ncc-fm.xls `
  --am .\artifacts\broadcast-staging\ncc-am.csv `
  --out .\artifacts\broadcast-staging\ncc-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\ncc-broadcast.ndjson `
  --out .\artifacts\broadcast-staging\taiwan_broadcast.idx `
  --source-date 2026-07-03
```

`taiwan_broadcast.idx` is TW-only. An optional preview merge
`merged-ca-gb-hk-tw.idx` may be built beside it without overwriting
`international_broadcast.idx`. HF overseas rows in the AM table emit
`service=shortwave`. Dual-track + NRTA gated policy is unchanged.

## RDS PS / PI is REFERENCE data — never a live-signal claim

The record contract permits ``rds_ps`` (8-character Programme Service name)
and ``rds_pi`` (4-character hex Programme Identification code) on any
station where the upstream regulator or curated source publishes them.
**Every RDS value in an ORCBRD1 pack is reference / expected data, never
a claim about the RF signal a device is currently receiving.**

The C++ ``StationCard`` (in `apps/orcsdr-tab5/ui/broadcast_station_db.hpp`)
carries a separate ``live_rds_agrees`` boolean that the FM runtime sets
to ``true`` only after the receiver has actually decoded a matching PS
or PI in the live RF signal. Downstream UI must draw a visible
distinction:

- **Reference (unconfirmed):** ``rds_ps`` / ``rds_pi`` present in the
  pack, ``live_rds_agrees == false``. Display value as "expected PS: BBC R2"
  or similar — never as if the RF signal has been identified.
- **Confirmed live:** ``rds_ps`` / ``rds_pi`` present and
  ``live_rds_agrees == true``. Downstream UI may promote it to
  "identified as: BBC R2".
- **Absent:** ``rds_ps`` / ``rds_pi`` empty in the pack. The UI must
  never fabricate one from the callsign or other columns.

Attribution for reference RDS follows the record's ``source`` field.
Current coverage of RDS reference data:

| Source | rds_ps | rds_pi | Notes |
| --- | ---: | ---: | --- |
| ofcom_txparams (UK) | ~93% | ~94% | Ofcom publishes both columns in its VHF CSV |
| fcc_lms (US) | 0 | 0 | Not published; PI could in principle be derived from callsigns via NRSC-4-B Annex D but that derivation is not implemented — no derived PI is written to the pack today |
| every other Tier 1 source | 0 | 0 | Regulator does not publish RDS |
| Wikipedia (Tier 3) | 0 | 0 | List-tables do not include PS/PI; individual station infoboxes sometimes do but are not parsed by this pack |

If a future enrichment pass fills PS/PI from a source different from
the record's base ``source`` (for example, mixing Wikipedia-supplied
PI codes into an FCC LMS record), add an ``rds_source`` field to that
record naming the RDS-specific source. Do not silently overwrite RDS
data with lower-tier information.

## Japan (MIC regional-bureau HTML lists) — Kanto + Kinki slice staged

**Status: staged.** 163 records (44 AM · 116 FM · 3 shortwave) covering
14 of 47 都道府県 across the Kanto and Kinki regions (Tokyo, Saitama,
Chiba, Kanagawa, Ibaraki, Tochigi, Gunma, Yamanashi + Osaka, Kyoto,
Hyogo, Nara, Shiga, Wakayama). Roughly 60 % of Japan by population.

Ledger row: `mic_regional_bureau_lists`, `release_allowed: true`,
`redistribution: Public Data License v1.0 (公共データ利用規約)` per
<https://www.soumu.go.jp/menu_kyotsuu/policy/tyosaku.html> —
attribution and modification disclosure required.

Why this endpoint instead of the Web-API: `tele.soumu.go.jp`
(the API host recorded under `mic_radio_use_web_api`) is currently
DNS-unresolvable globally — Google DNS `8.8.8.8` and Cloudflare
`1.1.1.1` both return no A/AAAA record — while the parent
`www.soumu.go.jp` still resolves and serves each regional bureau's
own broadcaster list under the same MIC / PDL 1.0 license basis.

Sources fetched (all Shift-JIS HTML):

| Label | URL | Bytes |
| --- | --- | --- |
| `kanto_list` | `www.soumu.go.jp/soutsu/kanto/bc/radio/list/index.html` | 85,500 |
| `kinki_tyuha` (AM) | `www.soumu.go.jp/soutsu/kinki/housou/radio/tyuha.html` | 45,731 |
| `kinki_keniki` (FM prefectural) | `www.soumu.go.jp/soutsu/kinki/housou/radio/keniki.html` | 45,310 |
| `kinki_fmhokan` (Wide-FM / 補完中継局) | `www.soumu.go.jp/soutsu/kinki/housou/radio/fmhokan.html` | 42,824 |

Build the JP slice:

```powershell
python .\tools\data_catalog\normalize_mic_regional_bureaus.py `
  --stage .\artifacts\broadcast-staging\mic_regional `
  --out   .\artifacts\broadcast-staging\mic_regional\mic-regional-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\mic_regional\mic-regional-broadcast.ndjson `
  --out   .\artifacts\broadcast-staging\mic_regional\jp_broadcast.idx `
  --source-date 2026-04-01
python .\tools\data_catalog\build_broadcast_index.py inspect `
  --input .\artifacts\broadcast-staging\mic_regional\jp_broadcast.idx
```

The normalizer reads each staged HTML file with a per-page parser
(Kanto index → 4 table shapes: AM full-power, shortwave, FM full-power,
prefecture-grouped community FM; Kinki three separate per-service
pages). It emits canonical ORCBRD1 station-card fields (`id`, `source`,
`source_id`, `service`, `frequency_hz`, plus `name`, `city`, `region`,
`country="JP"`, `power_w` when supplied by the Kinki pages, `status`).
AM frequencies are snapped to the nearest kHz, FM to the nearest 100 kHz
channel grid (76.1–108.5 MHz — Japan FM begins at 76 MHz, wider than
the US 88 MHz start), shortwave to the nearest kHz. Records are
deduplicated on stable `id`.

Attribution on-device must include the ledger's
`attribution_required` string:
> 出典：総務省ホームページ (該当ページのURL) — this data pack is
> derived from lists published by the Ministry of Internal Affairs and
> Communications (Kanto Regional Bureau and Kinki Regional Bureau)
> under 公共データ利用規約 v1.0. Modifications: HTML tables were
> extracted into ORCBRD1 station-card NDJSON.

Known limitations of the Phase-1 slice:

- **Coordinates are not present** on any of the MIC regional-bureau
  HTML tables. The ORCBRD1 record contract permits `latitude_e7` /
  `longitude_e7` to be absent when the source does not supply them.
- **Region is blank on ~44 Kanto AM/FM full-power rows** because the
  source table groups broadcasters without a per-row prefecture column;
  Kinki rows carry region from 送信場所.
- **`power_w` is only present on Kinki rows** (7.4 KB of the 163 rows).
  Kanto tables do not publish per-station power.
- **Not national.** Hokkaido, Tohoku, Shinetsu, Hokuriku, Tokai,
  Chugoku, Shikoku, Kyushu, and Okinawa bureaus each publish their
  broadcaster lists at different URL layouts; each needs a discovery
  + adapter pass to reach national coverage. Tokai bureau in particular
  links three regional PDFs at `www.soumu.go.jp/main_content/001066765.pdf`
  (AM 中波), `.../000935323.pdf` (FM 超短波), and `.../000935324.pdf`
  (Community FM) that are viable Phase-2 inputs.

## France (ANFR — Installations radioélectriques >5 W) — staged, rights verified

**Status: staged.** 11,179 French FM records with operator name for every station and coordinates for 11,178/11,179 (99.99%). Ledger row `anfr_installations_radioelectriques`, `release_allowed: true`.

- **Publisher:** Agence Nationale des Fréquences (ANFR)
- **Catalog:** <https://www.data.gouv.fr/fr/datasets/donnees-sur-les-installations-radioelectriques-de-plus-de-5-watts-1/>
- **Downloads:** monthly refresh at `static.data.gouv.fr/…/20260831-export-etalab-data.zip` (66 MB) + `…-ref.zip` (5 KB); latest snapshot 2026-08-31.
- **License:** Licence Ouverte 2.0 (Etalab) — CC BY 4.0 compatible.
- **Attribution:** "Source: Agence Nationale des Fréquences (ANFR), Données sur les installations radioélectriques de plus de 5 watts, publiées sur data.gouv.fr sous Licence Ouverte 2.0."
- **Filter:** Multi-table relational dump; filter `EMR_LB_SYSTEME == 'FM'` in `SUP_EMETTEUR` to isolate FM broadcast (11,179 rows). Excludes Aviation Civile / Défense / Intérieur transmitters (source itself already excludes them).
- **Join chain:** SUP_EMETTEUR → SUP_BANDE (freq) → SUP_ANTENNE → SUP_SUPPORT (DMS→signed decimal coords) → SUP_STATION → SUP_EXPLOITANT (operator name).

```powershell
python .\tools\data_catalog\normalize_anfr_broadcast.py `
  --data-zip .\artifacts\broadcast-staging\anfr-data.zip `
  --ref-zip  .\artifacts\broadcast-staging\anfr-ref.zip `
  --out      .\artifacts\broadcast-staging\anfr-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\anfr-broadcast.ndjson `
  --out   .\artifacts\broadcast-staging\fr_broadcast.idx `
  --source-date 2026-08-31
```

## Switzerland (BAKOM Swiss radio and TV broadcasters) — staged, rights verified

**Status: staged.** 288 FM records with full coordinates and power for every
station. Ledger row `bakom_radio_fernsehsender`, `release_allowed: true`.

- **Publisher:** Bundesamt für Kommunikation (BAKOM) — Federal Office of Communications.
- **Catalog:** <https://opendata.swiss/en/dataset/schweizerische-radio-und-fernsehsender>.
- **Download URL:** `data.geo.admin.ch/ch.bakom.radio-fernsehsender/radio-fernsehsender/radio-fernsehsender_2056_en.json` — 221 KB GeoJSON, HTTP 200, no auth.
- **Licence:** `opendata.swiss/en/terms-of-use#terms_open` — free reuse for any purpose (including commercial), source citation required.
- **Attribution required:** "Source: Bundesamt für Kommunikation (BAKOM) — Swiss radio and TV broadcasters (ch.bakom.radio-fernsehsender), retrieved from data.geo.admin.ch under opendata.swiss terms_open."
- **Scope filter:** Each of the 440 transmitter sites carries a parallel comma-separated `service` / `program` / `freqchan` triple listing every service hosted at that site (DAB+, DVB-T, RADIO). The normalizer keeps only entries where `service == "RADIO"` (analogue FM) — DAB+ and DVB-T are excluded. Switzerland shut down its last AM broadcaster (Beromünster) in 2008, so no AM records.
- **Coordinate handling:** Source coordinates are Swiss LV95 (EPSG:2056), reprojected to WGS84 via `pyproj` for the ORCBRD1 `latitude_e7` / `longitude_e7` fields.

Build the CH slice:

```powershell
python .\tools\data_catalog\normalize_bakom_broadcast.py `
  --input .\artifacts\broadcast-staging\bakom-radio-fernsehsender.geojson `
  --out   .\artifacts\broadcast-staging\bakom-radio-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\bakom-radio-broadcast.ndjson `
  --out   .\artifacts\broadcast-staging\ch_broadcast.idx `
  --source-date 2026-09-10
python .\tools\data_catalog\build_broadcast_index.py inspect `
  --input .\artifacts\broadcast-staging\ch_broadcast.idx
```

## New Zealand (RSM Register of Radio Frequencies) — rights flipped, raw archive staged

**Status: rights flipped, raw archive staged, no records yet.** Ledger row `nz_rsm_rrf`, `release_allowed: true` (flipped 2026-09-10 on operator legal judgment: OrcSDR ships a thin-client tool where the user selects location and consumes station data for their own personal / in-home listening — the RSM "personal or in-house use" condition is satisfied at the user endpoint, OrcSDR is the delivery mechanism, not the re-publisher. Attribution obligation flows through to the on-device UI). Raw `prism.zip` on disk with SHA-256 receipt; **no normalized NDJSON or ORCBRD1 index yet** — the inner `prism.mdb` is Microsoft Access 2000 format and requires `pyodbc` + the MS Access ODBC driver (Windows) or `mdbtools` (Unix) to extract. That's the remaining engineering step to actually put NZ records on-device.

- **Official source:** Radio Spectrum Management (Ministry of Business, Innovation and Employment) publishes the Register of Radio Frequencies (RRF).
- **Downloadable file:** `rsm.govt.nz/assets/Uploads/documents/prism/prism.zip` — 51 MB Microsoft Access 2000 database, HTTP 200, no auth, but **stale** (`Last-Modified: 2022-12-04`).
- **RSM copyright** (verified 2026-09-10 at `rsm.govt.nz/copyright`): material "may be reproduced for personal or in-house use" with acknowledgement — **narrower than CC BY 4.0**, does not clearly authorise redistribution in an OrcSDR device pack.
- **Authenticated paths:** CSV export via RRF search requires RealMe login and "approved user" status; API portal at `api.business.govt.nz/api/radiospectrum-management` requires signed keys.
- **data.govt.nz** CKAN API returned Imperva interstitial in-session; couldn't verify whether the same dataset has a broader NZGOAL/CC BY 4.0 licence there.
- **What must close to unblock:** RSM written confirmation that RRF may be redistributed under CC BY 4.0 or NZGOAL, or a data.govt.nz mirror with explicit open licence.
- **Operator-supplied archive on disk (gated stage track):** the 51 MB `prism.zip` (SHA-256 `44a5c0cc9ac72bbda8f2a3c31119c441b153820dddbfca33385bc9a84ee10d04`, containing a 363 MB `prism.mdb` Microsoft Access 2000 database) is staged at `artifacts/broadcast-staging/prism.zip` with a `prism.zip.receipt.json` marked `release_allowed: false, stage_only: true`. This is present for tooling and future rights review only — it must **never** be included in a signed catalog pack while the copyright question is unresolved. Parsing MDB requires `pyodbc` + the Microsoft Access ODBC driver (Windows only) or `mdbtools` (Unix); no normalizer has been written yet.

## Germany (BNetzA Rundfunk) — rights blocked, no data staged

**Status: no records acquired, no records staged, no shippable pack.**
Ledger entry `bnetza_rundfunk_gbg` sits at `release_allowed: false`.

- **Official source:** Bundesnetzagentur (BNetzA) — Rundfunk landing
  page <https://www.bundesnetzagentur.de/DE/Fachthemen/Telekommunikation/Frequenzen/OeffentlicheNetze/Rundfunk/start.html>.
  UKW (FM), DAB, and DVB-T transmitter datasets are described in
  XLSX / ZIP / XML format specs (Formatbeschreibung, Stand 3. Juli
  2025) but the datasets themselves are **not** published openly.
- **Access barrier:** BNetzA distributes the transmitter data only to
  a **Geschlossene Benutzergruppe** (closed user group) —
  "GBG-Rundfunksenderdaten." Access requires the paper application
  form "Informationsblatt und Antrag zur Aufnahme in die
  GBG-Rundfunksenderdaten" (85 KB PDF linked from the Rundfunk
  landing page) and BNetzA approval as a *berechtigte Nutzer*.
- **No open-data licence found (verified 2026-09-10):** the BNetzA
  Rundfunk pages carry no CC BY, DL-DE-BY-2.0, DL-DE Zero, or GovData
  reuse statement. A GovData.de search for
  "Bundesnetzagentur Rundfunk" returned no matching broadcast-transmitter
  dataset — the only BNetzA GovData listing is
  `bundesnetzagentur-mobilfunkmonitor` (mobile carrier monitoring,
  not broadcast). BNetzA's public
  [EMF-Datenbank](https://www.bundesnetzagentur.de/emf) covers only
  mobile / cellular / amateur radio site certifications, not broadcast.
- **Non-authoritative mirrors are out of scope by task rule.** The
  community project `ukwtv.de` (UKW/TV-Arbeitskreis e.V.) publishes a
  frequency list derived from BNetzA data, but as a Verein
  (crowdsourced association) it falls under the task's explicit
  exclusion of commercial directories, Radio Browser, SDR logs, and
  crowdsourced station lists as authoritative sources.
- **Terms gap that must close before any DE staging:** verbatim
  clauses on (a) whether GBG-member reuse permits publication in an
  ORCBRD1 device pack, (b) attribution wording, and (c) rate limits
  or update-cadence obligations. Absent an explicit open licence, the
  right resolution is a written BNetzA statement (or a
  Landesmedienanstalt with an open licence for its state's
  assignments).
- **Alternate path worth investigating (deferred):** each of Germany's
  14 Landesmedienanstalten (state media authorities — BLM Bayern, LfM
  NRW, LFK Baden-Württemberg, LPR Hessen, mabb Berlin-Brandenburg,
  etc.) publishes the FM assignments it grants inside its state. A
  future pass could scout each LMA site for an openly-licensed
  assignment CSV/PDF. That is 14 separate rights reviews — not done
  here.
- **What was deliberately not done in this task:** no requests made
  against BNetzA download endpoints; no GBG application filed by
  Claude on the operator's behalf; no `normalize_bnetza_*.py` written;
  no synthetic-source test; no staged NDJSON or ORCBRD1 index; no
  ledger `release_allowed` flip.

## Japan (MIC Radio Wave Utilization Portal Web-API) — rights verified, acquisition pending

**Status:** ledger entry `mic_radio_use_web_api` is now
`release_allowed: true`. No data has been fetched or staged yet — the
MIC endpoint returns HTTP 403 to non-interactive HTTPS clients, so an
operator with a browser session must do the actual pull. Once a raw
export is on disk, this doc will grow a fetch → normalize → build
recipe like the FCC and ISED sections above.

- **Official source:** Ministry of Internal Affairs and Communications
  (総務省) 電波利用ポータル 無線局等情報検索 Web-API. Catalog:
  <https://www.tele.soumu.go.jp/j/musen/webapi/>. Terms:
  <https://www.tele.soumu.go.jp/j/musen/webapi/kiyaku/index.htm>.
  Request conditions (schema of parameters and response fields):
  <https://www.tele.soumu.go.jp/resource/j/musen/webapi/mw_req_conditions.pdf>.
- **License basis (verified 2026-09-10 from an operator-supplied
  English translation of the terms page):** Article 4 of the MIC Web-API
  利用規約 binds content reuse to the **Government Standard Terms of
  Use** (政府標準利用規約) — attribution required, modification
  disclosure required, redistribution and commercial use permitted,
  CC BY 4.0 compatible. Article 3 mandates a specific disclaimer
  attached to any downstream service:
  > "This service is created based on information obtained using the
  > Web-API function of the Ministry of Internal Affairs and
  > Communications' Radio Wave Utilization Portal, but the content of
  > the service is not guaranteed by the Ministry of Internal Affairs
  > and Communications."
  Article 6.3 prohibits "massive access in a short period of time" —
  a one-time paced fetch to build an offline snapshot is compatible,
  a hammering fetch or a scheduled crawl is not. Article 7 disclaims
  accuracy; the ORCBRD1 record contract's existing "frequency match is
  a reference result, never proof of identity" wording already covers
  this. Article 10 places disputes under Japanese law / Tokyo District
  Court.
- **Nature of the source:** REST search API returning JSON, XML, or
  CSV per query. It is **not** a single bulk download. Full
  terrestrial AM/FM coverage is achieved by iterating structured
  queries — the natural axis is the 47 都道府県 (prefectures) crossed
  with the terrestrial-broadcast service filter — and concatenating
  the responses. Any acquisition script must respect Article 6.3
  (pace requests, no parallel hammering, single pass).
- **Scope for OrcSDR:** terrestrial Japanese AM (中波) and FM
  (超短波 / V-Low) broadcast only. Exclude TV (テレビ), amateur
  (アマチュア), aviation, marine, cellular / mobile, satellite, and
  shortwave (短波). Emit canonical ORCBRD1 station cards with
  `country: "JP"`, a stable official identifier (免許番号 licence
  number, or the record's official row ID) as `source_id`, and only
  source-provided factual fields (callsign/name, prefecture as
  `region`, city, coordinates, power, status, mode). Never generate
  IDs from station names alone. Never claim a frequency match proves
  reception or identity.
- **Attribution obligation on-device:** the on-device UI or any
  downstream OrcSDR surface that displays records from this pack must
  carry the Article 3 disclaimer verbatim (or a faithful
  Japanese/English equivalent). It is stored on the ledger row as
  `attribution_required`; the future signed catalog pack should
  surface it in its manifest.
- **Operator follow-up required to unblock staging:**
  1. Open the request-conditions PDF
     (<https://www.tele.soumu.go.jp/resource/j/musen/webapi/mw_req_conditions.pdf>)
     in a browser and capture the request-parameter names and
     response-field names for terrestrial broadcast queries. Drop the
     PDF (or a text extract) at
     `artifacts/broadcast-staging/mic_web_api_conditions.pdf` so a
     deterministic normalizer can be written without guessing.
  2. Perform a single-pass paced fetch of every 都道府県 × terrestrial
     AM/FM query in CSV or JSON format. Concatenate the raw output as
     `artifacts/broadcast-staging/mic-radio-broadcast.raw.<json|csv>`.
     Record the exact endpoint URL, all request parameters used, and
     the retrieval timestamp.
  3. Hand the raw file to
     `tools/data_catalog/fetch_broadcast_source.py --source
     mic_radio_use_web_api --file <raw>
     --out artifacts/broadcast-staging/mic-radio-broadcast.raw` so the
     SHA-256 receipt is generated with the ledger's rights statement.
- **What was deliberately not done in this task:** no records fetched
  from `tele.soumu.go.jp` in-session (the endpoint is 403 to
  non-interactive HTTPS and the task forbids access-control bypass);
  no `normalize_mic_broadcast.py` yet (writing it requires the
  request-conditions field list — the task forbids guessing column
  names); no synthetic-source normalizer test yet (would only lock in
  the guess); no Japan-staged NDJSON or ORCBRD1 index; no merge with
  CA / GB / US slices; no signing, publishing, pushing, or hardware
  flash.

## Hong Kong (OFCA analogue sound frequency table) - SHIP track

**Status:** ledger entry `ofca_sound_freq_table` is `release_allowed: true`
(DATA.GOV.HK Terms and Conditions allow free commercial/non-commercial
redistribution with attribution). Country code is **`HK`**, not `CN`.

- **Catalog:** [Frequency Table for Analogue Sound Broadcasting Services in Hong Kong](https://data.gov.hk/en-data/dataset/hk-ofca-ofca-ofca-dataset-21)
- **English CSV (exact download URL):**
  `https://www.ofca.gov.hk/filemanager/ofca/common/datagovhk/analogue_sound_broadcasting_frequency_en.csv`
- **Terms / attribution:** [DATA.GOV.HK Terms and Conditions](https://data.gov.hk/en/terms-and-conditions) - attribute the Government of the HKSAR, Office of the Communications Authority, and DATA.GOV.HK.
- **Columns:** `BROADCASTER`, `CH_NAME`, `TX_STATION`, `MODULATION` (`VHF/FM` or `MF/AM`), `FREQ` (MHz; AM as `0.567`), `ERP` (watts), tunnel rebroadcast notes, remarks. No coordinates in the CSV.
- **Staging (HK-only index first; do not overwrite `international_broadcast.idx` until merge reviewed):**

```powershell
python .\tools\data_catalog\fetch_broadcast_source.py --source ofca_sound_freq_table `
  --out .\artifacts\broadcast-staging\ofca-analogue-sound-frequency-en.csv
python .\tools\data_catalog\normalize_ofca_broadcast.py `
  --input .\artifacts\broadcast-staging\ofca-analogue-sound-frequency-en.csv `
  --out .\artifacts\broadcast-staging\ofca-broadcast.ndjson
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\ofca-broadcast.ndjson `
  --out .\artifacts\broadcast-staging\hongkong_broadcast.idx `
  --source-date <YYYY-MM-DD>
```

Optional CA+GB+HK merge **preview** (keeps existing CA+GB artifacts):

```powershell
Get-Content .\artifacts\broadcast-staging\ised-broadcast.ndjson,
  .\artifacts\broadcast-staging\ofcom-broadcast.ndjson,
  .\artifacts\broadcast-staging\ofca-broadcast.ndjson |
  Set-Content .\artifacts\broadcast-staging\merged-ca-gb-hk-broadcast.ndjson -Encoding utf8
python .\tools\data_catalog\build_broadcast_index.py build `
  --input .\artifacts\broadcast-staging\merged-ca-gb-hk-broadcast.ndjson `
  --out .\artifacts\broadcast-staging\merged-ca-gb-hk.idx `
  --source-date <YYYY-MM-DD>
```

## China (NRTA prefecture directory) - rights gated; not a transmitter DB

**Status: ledger entry only (`release_allowed: false`). No shippable ORCBRD1
pack. No `normalize_nrta_broadcast.py`. No files under
`artifacts/broadcast-staging/` for this source.**

- **Official source:** National Radio and Television Administration (国家广播电视总局)
  column [播出机构（频道）](https://www.nrta.gov.cn/col/col69/index.html).
- **Current available edition inspected:**
  [地级以上广播电视播出机构及频道频率名录（截至2026年6月30日）](https://www.nrta.gov.cn/art/2026/7/7/art_69_73655.html)
  (published 2026-07-07). Attached XLS via NRTA `downfile.jsp`
  (`filename=2607081604346344.xls`).
- **Primary candidate URL** `art/2026/4/7/art_69_73007.html` (as-of 2026-03-31)
  returned **网站维护中** to this session; do not treat that as proof the
  edition never existed - secondary press summarized it on 2026-04-08.
- **What the directory is:** a prefecture-level-and-above **broadcast
  institution / licensed channel-and-program roster**. Sheet `全部地级台`,
  386 institution rows. It is **not** a transmitter registry with site
  coordinates, ERP/power, or live on-air status.
- **Chinese columns → English:**
  - `省份` → province / region label (includes `中央`, provinces, `兵团`)
  - `级别` → administrative level (`国家`, `省`, `省会`, `地市`, `单列市`)
  - `台名` → broadcast institution name
  - `许可证编号` → licence number (stable official id candidate)
  - `节目设置` → program-lineup prose (TV + radio **program names**)
  - `广播频率` → **count** of radio programs (not Hz)
  - `电视频道` → **count** of TV channels
- **AM/FM fitness:** inspected file has **zero** kHz/MHz/兆赫/千赫 tokens.
  Radio content is names such as `新闻广播` / `中国之声` inside `节目设置`;
  `广播频率` summed to 1001 program slots across 367 institutions with a
  non-zero count. ORCBRD1 cannot be populated with honest `frequency_hz`
  values from this spreadsheet without inventing frequencies.
- **Terms (`terms_url`):** [网站声明](https://www.nrta.gov.cn/col/col226/index.html)
  (captured 2026-09-10 PT). Verbatim core clauses: site materials are
  provided by NRTA and related units; media/websites/commercial bodies must
  not commercially republish site content in original form, nor distort it;
  copyright belongs to the site; unauthorized republication of information
  supplied by other units requires contacting the site for **legal
  authorization**. No CC/OGL/open-government licence found. Redistribution
  of extracted factual rows into an OrcSDR catalog is **not** approved.
- **Scout-only artifact (not staging):** optional local copy under
  `artifacts/nrta-rights-scout/` for rights review; SHA-256
  `5ace33d19974e7b4c33d3f84029b886962d270057b89face94090db52ebff2e2`
  (135168 bytes). Do not promote into `broadcast-staging/` or a signed pack.
- **Omissions / out of scope:** county-level roster (`art_69_73654`),
  education-TV roster (`art_69_73653`), TV-only program lines, international
  shortwave language services listed as program names without frequencies,
  and any commercial/crowdsourced CN frequency databases (deliberately not
  substituted).
- **Required before any shippable import:** written NRTA redistribution
  permission **and** a source that actually supplies AM/FM carrier
  frequencies (this directory does not).
