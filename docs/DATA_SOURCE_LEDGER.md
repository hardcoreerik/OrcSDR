# Data source eligibility ledger

| Pack | Publisher | Retrieval | Redistribution gate | Cadence | Status |
| --- | --- | --- | --- | --- | --- |
| FAA aircraft | FAA Aircraft Registry | Releasable Aircraft Database | public-source verification at publish time | daily | approved source, pack not published |
| FAA aviation | FAA Aeronautical Information Services | NASR subscription CSV | public-source verification at publish time | 28 days | approved source, pack not published |
| NOAA weather | NOAA/NWS | National transmitter data | public-information verification at publish time | source-dependent | curated CSV adapter required |
| FCC FM/AM | FCC | current audio-station data export | public-data verification at publish time | source-dependent | curated CSV adapter required |
| P25 profiles | user or named public publisher | local import/export or signed `p25_...` pack | source URL, retrieval date, transformation record, and explicit redistribution permission required; RadioReference data is not bundled | source-dependent | format supported; no system pack published |
| World coastlines (embedded) | Natural Earth | 110m coastline GeoJSON | public domain | static with firmware | embedded ORCMAP1 default |
| Regional map packs | OpenStreetMap contributors | Overpass / signed catalog | ODbL attribution on draw | per-pack | catalog `lane_county_map` → `/orcsdr/data/regional_map.idx` |
| Maps | user/Companion | SD or authenticated import | attribution/manifest validation | user-managed | separate feature |
| HF schedules | HFCC | public schedule data | explicit terms review required | seasonal | deferred |
| LoRa profiles | LoRa Alliance | regional parameters | standards/license review required | revision-based | deferred |

Every published pack must add the source URL, retrieval timestamp, license or
terms review, transformation command/version, SHA-256, and removal contact to
this ledger. A source with unclear redistribution terms is user-import only.
