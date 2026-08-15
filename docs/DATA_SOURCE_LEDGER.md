# Data source eligibility ledger

| Pack | Publisher | Retrieval | Redistribution gate | Cadence | Status |
| --- | --- | --- | --- | --- | --- |
| FAA aircraft | FAA Aircraft Registry | Releasable Aircraft Database | public-source verification at publish time | daily | approved source, pack not published |
| FAA aviation | FAA Aeronautical Information Services | NASR subscription CSV | public-source verification at publish time | 28 days | approved source, pack not published |
| NOAA weather | NOAA/NWS | National transmitter data | public-information verification at publish time | source-dependent | format adapter pending |
| FCC FM/AM | FCC | LMS public database | public-data verification at publish time | source-dependent | format adapter pending |
| P25 | user | local `P25.cfg` import/export | no directory redistribution | user-managed | supported separately |
| Maps | user/Companion | SD or authenticated import | attribution/manifest validation | user-managed | separate feature |
| HF schedules | HFCC | public schedule data | explicit terms review required | seasonal | deferred |
| LoRa profiles | LoRa Alliance | regional parameters | standards/license review required | revision-based | deferred |

Every published pack must add the source URL, retrieval timestamp, license or
terms review, transformation command/version, SHA-256, and removal contact to
this ledger. A source with unclear redistribution terms is user-import only.
