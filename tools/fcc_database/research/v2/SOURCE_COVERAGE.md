# Coverage matrix — verified source descriptions, not ingested records
2026-11-27 UTC. Official ULS introduction https://wireless.fcc.gov/uls/documentation/pa_intro24.pdf (2014), field dictionary https://wireless.fcc.gov/wtbfiles/pa_ddef51.pdf (2018 printed pa_ddef50), current source catalog https://opendata.fcc.gov/Wireless/FCC-Universal-Licensing-System-ULS-/x28i-i4z4. Current ZIP directory and FCC weekly/daily page returned HTTP 403 to web retrieval; sandbox DNS unavailable. Historical field definitions are not a verified current schema. For ULS site records use system ID plus location, antenna and frequency number; compare assigned frequencies and preserve conflicts, never callsign-only cross joins. FR and EM are different tables (EM is emission, EN entity); market partitions and microwave paths have distinct keys. License-only amateur/GMRS/ship/aircraft records must not imply a transmitter assignment. Current archive coverage unknown; no counts, sizes or hashes of downloaded real data.

| Country/feed | Intended service coverage | Source/format | Missing or restricted scope | Acquisition |
|---|---|---|---|---|
| US ULS land mobile private/public | industrial/business, public safety, rail, trunked/conventional, fixed/mobile | service ZIP pipe-delimited HD/EN/LO/AN/FR/EM etc | not all licensed bands; current archives unverified | blocked |
| US ULS market-based, cellular, paging, BRS/EBS | licensed spectrum blocks, partitions and sites when published | ULS market and site tables | allocation not proof of deployed TX | blocked |
| US ULS microwave, coast/ground | fixed links, aviation ground, maritime coast | ULS site/path tables | preserve endpoints | blocked |
| US ULS amateur, GMRS, ship, aircraft, commercial operator | individual licensing | ULS form-specific tables | no inferred site or frequency assignments from license alone | blocked |
| US ULS leases | leased authorizations and parent links | ULS lease tables | avoid double counting parent and lease; preserve both | blocked |
| US LMS | AM/FM/TV, related broadcast | https://opendata.fcc.gov/Media/LMS-Public-Database-Files/nsck-y87u ; https://enterpriseefiling.fcc.gov/dataentry/public/tv/lmsDatabase.html | current export/schema/cadence not verified; CDBS historical separate | blocked |
| US ICFS/IBFS | space and earth station | https://www.fcc.gov/icfs ; https://catalog.data.gov/dataset/international-bureau-filing-system-ibfs | legacy IBFS not current ICFS; completeness unknown | not acquired |
| US ELS | experimental authorizations | https://www.fcc.gov/els | confidential/unpublished and bulk access unknown | not acquired |
| US FSS registrations | supplemental earth stations | https://catalog.data.gov/dataset/protected-fss-earth-station-registration-complete-dataset | registration only; overlap with ICFS | not acquired |
| US legacy CDBS/ITFS/MDS | historical broadcast and transitional | LMS/ULS references | do not override current authorization | not acquired |
| US NTIA/federal, exempt services | not full FCC licensing domain | outside public FCC station dataset | explicit excluded scope; absence is not illegal use | not applicable |
| CA ISED radio authorizations | land mobile, aero, maritime, fixed, earth, broadcast | https://ised-isde.canada.ca/site/spectrum-management-system/en/spectrum-management-system-data/download-sms-data | all-services dated 2026-11-03; excludes other feeds; no duplicate imports of individual component exports | blocked |
| CA ISED terrestrial/satellite spectrum site | deployed spectrum licence site reports | separate downloads same official page | submission/completeness constraints; not proof of all actual TX | not acquired |
| CA supplemental fixed, broadcast technical, amateur and maritime ID | technical/identification records | same site, separate registers | scope/terms/joins need confirmation; AFC/white-space feeds purpose-specific, not assumed redistributable | not acquired |
| AU ACMA | radiocomms register | https://www.acma.gov.au/radiocomms-licence-data | terms and current schema pending | not acquired |
| UK Ofcom | wireless telegraphy register | https://www.ofcom.org.uk/spectrum/frequencies/spectrum-information-portal | exemptions/coverage/terms pending | not acquired |
| NZ RSM | frequency register | https://www.rsm.govt.nz/licensing/how-do-i/use-the-rrf | bulk route unverified | not acquired |
| FR ANFR | installation data | https://www.data.gouv.fr/datasets/donnees-sur-les-installations-radioelectriques-de-plus-de-5-watts-1 | threshold excludes many stations; not full license register | not acquired |
| DE BNetzA | site/EMF public information | https://www.bundesnetzagentur.de/DE/Vportal/TK/Funktechnik/EMF/start.html | no verified complete bulk licensing export | not acquired |
| JP MIC | station information | https://api-catalog.e-gov.go.jp/info/en/apicatalog/view/46 | national bulk feasibility/terms pending | not acquired |
| BR ANATEL/MCom | broadcast and other radio licensing research | https://www.gov.br/mcom/pt-br/acesso-a-informacao/dados-abertos | other services not verified | not acquired |

US ULS/LMS catalog labels public domain; specific files, third-party material, privacy and international terms require review before publication. No real data redistributed. National package completeness cannot be declared. Lane County WQFV834/463.400 MHz remains unresolved, no hardcoded identity.
