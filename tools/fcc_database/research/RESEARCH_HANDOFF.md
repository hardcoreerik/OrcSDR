# FCC Database — source acquisition checkpoint
Date: 2026-11-26 Pacific
Base: 870454241c0d58af246755a5263a8e3ead6835c2
Scope: independent research tooling only; not a firmware API or a national package.

## Coordination
At inspection feature/fcc-database was identical to main and AGENTS.md returned 404 on that ref. No coordinator contract changes were visible. Direct git and regulator downloads from the sandbox failed DNS resolution. No approved binary format, lookup API or evidence policy was available to implement against. Research branch was created from the inspected base. RSA/REA inspection is outstanding.

## Source inventory (initial, not exhaustive)
- US ULS: https://data.fcc.gov/download/pub/uls/complete/ and https://data.fcc.gov/download/pub/uls/daily/ . Official documentation https://wireless.fcc.gov/uls/documentation/pa_intro24.pdf describes service-specific ZIPs and pipe-delimited tables. Weekly full and daily transaction feeds. Preserve license, location, antenna, frequency and emission keys; do not join solely by callsign. Obtain current field dictionary and actual files before implementing joins.
- US LMS: https://opendata.fcc.gov/Media/LMS-Public-Database-Files/nsck-y87u . Broadcast technical records and dictionary; actual current export, schema, cadence and coverage remain to verify.
- US ICFS: https://www.fcc.gov/icfs ; historical IBFS catalog https://catalog.data.gov/dataset/international-bureau-filing-system-ibfs . Satellite and earth stations. Do not treat old IBFS archive as a complete current ICFS snapshot.
- US ELS: https://www.fcc.gov/els . Public experimental records, excluding confidential material; bulk access unverified.
- US additional: https://catalog.data.gov/dataset/protected-fss-earth-station-registration-complete-dataset ; daily registration data, not a substitute for all satellite authorizations. Legacy ITFS/MDS: https://wireless.fcc.gov/cgi-bin/wtb-itfsmdsdata.pl ; historical transition data only.
- Canada: https://ised-isde.canada.ca/site/spectrum-management-system/en/spectrum-management-system-data/download-sms-data ; all-services extract link https://www.ic.gc.ca/engineering/SMS_TAFL_Files/TAFL_LTAF.zip . Monthly CSV per official field dictionary https://ised-isde.canada.ca/site/spectrum-management-system/sites/default/files/attachments/2022/tafl_description_ltaf.pdf . All-services listing covers aeronautical, broadcast, fixed, land mobile, maritime and satellite earth stations. Separate terrestrial/satellite spectrum-license site extracts also exist. Source page dated extract 2026-11-03. Dictionary columns (one-based): direction 1, MHz 2, frequency ID 3, regulatory service 4, emission 12, station label 32, callsign 34, station type 35, ITU class 36, reference ID 39, WGS84 latitude/longitude 41/42, radius km 46, authorization ID 48, status 52, licensee 55. Licensee address 56 is not station coordinates. Preserve RX vs TX; actual file header/encoding/row variants unverified. Do not double-count all-services plus individual extracts.
- Australia: https://www.acma.gov.au/radiocomms-licence-data ; full register download available, specific usage conditions must be reviewed.
- UK: https://www.ofcom.org.uk/spectrum/frequencies/spectrum-information-portal ; WTR CSV available; coverage and terms pending.
- New Zealand: https://www.rsm.govt.nz/licensing/how-do-i/use-the-rrf ; register exists, bulk rights and access pending.
- France: https://www.data.gouv.fr/datasets/donnees-sur-les-installations-radioelectriques-de-plus-de-5-watts-1 ; installation data with threshold, not full licensing coverage.
- Germany: https://www.bundesnetzagentur.de/DE/Vportal/TK/Funktechnik/EMF/start.html ; public site certificate/EMF lookup, not verified national frequency-license bulk export.
- Japan: https://api-catalog.e-gov.go.jp/info/en/apicatalog/view/46 ; radio station information API, national acquisition and licensing terms unverified.
- Brazil: https://www.gov.br/mcom/pt-br/acesso-a-informacao/dados-abertos ; broadcast open data; other ANATEL service coverage requires verification.

No source is cleared for redistribution by this work. Some US catalog entries identify public-domain licensing, but specific archive terms, third-party content and privacy need review. No regulatory data published. Federal NTIA, license-exempt uses, confidential and unavailable records cannot be treated as covered. All archive sizes, actual record counts and package statistics remain unknown.

## Implemented
tools/fcc_database/research/acquire.py is a developer-only ZIP acquisition and integrity inventory utility. Streaming SHA-256, optional supplied checksum, ZIP CRC via complete reads, per-member SHA-256, size limits, no extraction, content-addressed local output, source URL/HTTP metadata and provenance manifest. HTTPS transport check is not publisher authenticity, schema validation or redistribution clearance. Local file mode records that input is local; original source URL must be documented separately. No retry/resume, archive entry-count cap, robust concurrent publication, symlink member classification, HTTPS downgrade preflight, or national-scale qualification yet. Use trusted official inputs; this is not a hardened untrusted-upload service.

## Reproduce
From repository root:
    cd tools/fcc_database/research
    python -m unittest -v
    python acquire.py ised-all --url https://www.ic.gc.ca/engineering/SMS_TAFL_Files/TAFL_LTAF.zip --output /path/outside/repo/raw
    python acquire.py uls-example --local /path/to/official.zip --output /path/outside/repo/raw

Run using Python 3.8+; keep raw archives outside git. Supply --sha256 when a trusted expected digest exists. A self-computed hash is not independently authenticated. Inventory directory contains archive.zip and manifest.json. Use the full official source inventory, not just the example archive, for national validation.

## Test evidence
Executed locally Python 3.13: 8 synthetic tests passed (0.014 s). See TEST_RESULTS.log. Git blob IDs checked against sandbox files:
acquire.py 27a42e41f4613ae8ef478d9ce121c968a241873a
test_acquire.py 98aa198199e99efb6216318395a607cc65b9b938
No network success, real source ingestion, national benchmark, hardware or C++ core test was performed.
FCC complete directory web fetch returned 403; Canadian ZIP web fetch timed out. Sandbox Python requests to both returned DNS errors. No official WQFV834 record was retrieved; 463.400 MHz Lane County case remains unverified.

## Coordinator handoff
This is a partial checkpoint, not acceptance or merge readiness. Publish approved format, API, evidence policy, acceptance criteria and ownership paths; identify exact contract commit. Arrange network-enabled source acquisition and retain archive hashes, source timestamps and counts. Then implement source-specific schema validation/normalization and native lookup against the approved contracts. No production interfaces invented.

## Independent testing handoff
Pin the PR head; run the eight synthetic acquisition tests. Verify exact code blob hashes above. Extend adversarial archive testing independently (CRC corruption, truncated archive, duplicate normalized paths, excessive member counts, redirect and interrupted downloads). Do not infer actual station coverage from fixtures. Once full official files are available, check joins independently against raw keys, TX/RX separation, range semantics, and the Lane County case. No national or embedded performance targets have been validated.
