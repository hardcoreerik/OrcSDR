# Live VDL Mode 2 capture, 2026-10-29, Tab5 on 136.975 MHz

Receiver: OrcSDR Tab5 (COM17), native VDL2 decoder, 250 kS/s stream. About 06:23 to 06:35 UTC. Every message below is a CRC-valid AVLC
frame carrying ACARS. Screenshots are the message detail pages (`message-00.png` is the newest, `message-09.png` the oldest);
`live-list.png` is the LIVE list and `channels-page.png` the channel list with the new VDL2 preset.

Device counters at the time (`RTL_ACARS STATUS`): 18 CRC-valid AVLC frames (10 ACARS, 8 with no data), 34 valid VDL2 headers,
25 error-correction passes, 1 rejected frame, no dropped messages, no stream gaps.

## Messages (text exactly as received)

| # | UTC | Tail | Label | Block | Text |
|---|---|---|---|---|---|
| 00 | 06:34:54 | N244SY | H1 | 1 | `C87AOO137C#CFBFDE0633591029ABZ 06315707301    0    0 06315707308    0    0 06320507310    0    0` |
| 01 | 06:34:09 | N244SY | H1 | 1 | same as 00 (resent) |
| 02 | 06:33:55 | N338DU | Q0 | 9 | `S50ADL0661` |
| 03 | 06:33:45 | N244SY | 32 | 0 | `M19AOO137CP,E,2026-10-29 06:33:43,3870,65654,N 44.221 W 123.107,3723,232,011,3,163,171,263,0636,` |
| 04 | 06:31:45 | N244SY | 32 | 9 | `M18AOO137CP,E,2026-10-29 06:31:43,3920,65704,N 44.204 W 122.970,5491,246,017,0,204,217,337,0636,` |
| 05 | 06:30:28 | N244SY | H1 | 8 | `C86AOO137C#CFBFDE0630151029ABZ 06281417294    0    0 06281517402    0    0 06283617310    0    0 06283617301    0    0 06283617308    0    0 06295107402    0    0` |
| 06 | 06:30:25 | N244SY | H1 | 7 | same as 05 with message number C85 |
| 07 | 06:29:45 | N244SY | 32 | 6 | `M17AOO137CP,E,2026-10-29 06:29:43,3970,65754,N 44.182 W 122.794,7321,235,023,-4,254,270,423,0636,` |
| 08 | 06:25:45 | N244SY | 32 | 4 | `M15AOO137CP,E,2026-10-29 06:25:43,4080,65864,N 44.102 W 122.399,11581,249,029,-9,274,297,468,0636,` |
| 09 | 06:23:45 | N244SY | 32 | 9 | `M10AOO137CP,E,2026-10-29 06:23:43,4140,65924,N 44.045 W 122.171,14097,249,045,-13,333,368,585,0635,` |

Flight OO137C is SkyWest. The label-32 track descends toward Eugene, Oregon (the ground station that sends squitters on this channel is
EUG at 44.117N 123.217W).

## What the interpreter decodes now, and what it does not

- Label 32: date, time and position are decoded (the time matches the receive time to two seconds). The ten other numbers are shown as
  received. Hypotheses from the five samples, **not verified**: 14097/11581/7321/5491/3723 fall like an altitude in feet on the descent;
  249/249/235/246/232 look like a speed in knots; -13/-9/-4/0/3 rise as the aircraft descends, like an outside air temperature in
  degrees C; 4140 and 65924 move together (65924 - 4140 = 61784 in every sample), which fits fuel on board and gross weight in pounds;
  045/029/023/017/011 fall toward zero near the destination (not simple distance to the airport). Nothing documents these.
- `H1 #CFB FDE...` (central fault display event list): structure decoded (report time, `1029ABZ` tag, entries of hhmmss + five digit
  code + two zero numbers). Codes are shown as received. In the samples the same four-digit codes appear with a leading `1` at
  06:28:36 (7310, 7301, 7308) and with a leading `0` at 06:31:57 to 06:32:05, and 7402 appears as `17402` at 06:28:15 and `07402`
  at 06:29:51, which suggests the leading digit is on/off. 7294 is only seen with `1`. Not verified.
- `Q0`: link test, decoded.

## Cross-reference with web sources (2026-10-29)

| Check | Source | Result |
|---|---|---|
| Tail N244SY is a SkyWest Embraer ERJ 170-200 | FAA registry (Mode S hex A24082) | confirmed |
| Flight OO137C is callsign SKW137C | OpenSky flights API for A24082 | confirmed: callsign `SKW137C`, departed KSLC (first seen 04:55:13Z), last seen 06:38:15Z |
| Last seen a few minutes after our last report (06:34Z), near Eugene | OpenSky | consistent with landing at Eugene (EUG) |
| Delta link test: N338DU, flight DL0661 | adsb.lol live feed | confirmed: N338DU is `DAL661` (BCS3), airborne over Oregon at 37,875 ft |
| Label 32 track ends at Eugene (EUG 44.1246N 123.2120W) | our five positions | distance falls 45.1, 35.1, 18.3, 11.5, 7.3 nm |
| Field after the position is altitude in feet | community note ("altitude in feet?") and our data | descent rates between reports 1258, 1065, 915, 884 fpm while closing on EUG: consistent |
| The speed-like field (249, 249, 235, 246, 232) is ground speed | position legs | **no**: implied ground speed is 312, 265, 231, 180 kt; the field is something else |
| `FDE` is Flight Deck Effect; four digits after the time are month-day | community CFB page and our samples | consistent (`1029` = 29 Oct; community `0805` beside a 2020-08-05 report) |

Not done: the position track of N244SY from a public source. Anonymous OpenSky track and state history endpoints return 404 or need an
account, and adsb.lol/airplanes.live trace files return 403 to scripts. `tools/acars_adsb_xref.py` compares a report with the live
adsb.lol position, so it only works within a minute or two of the report. Credentials for any research account stay in environment
variables on the researcher's own machine and never go into code.

## Second live capture, 08:14 UTC, matched live against ADS-B

After 56 minutes with no decoded message, the Tab5 decoded four ACARS blocks on 136.975 MHz at 08:14:33 to 08:14:44 UTC (plus
four AVLC frames with no data, counted only). Screenshots: `live2-0814-list.png`, `live2-0814-details.png`.

| UTC | Tail | Flight | Label | Block | Text |
|---|---|---|---|---|---|
| 08:14:44 | N395CM | 8C3337 | H1 | 6 | `F12A8C3337#M1B/BA USADCXA.AT1.N395CM651483A8002AD7` |
| 08:14:40 | N395CM | 8C3337 | H1 | 6 | same (resent) |
| 08:14:37 | N395CM | 8C3337 | `_?` | 5 | `S30A8C3337`, acknowledges uplink block O |
| 08:14:33 | N395CM | 8C3337 | `_?` | 5 | `S30A8C3337`, acknowledges uplink block O |

Cross-reference, run at 08:15:25 UTC, about 40 seconds after the last block:

| Check | Source | Result |
|---|---|---|
| N395CM is flight 8C3337 | adsb.lol live feed | confirmed: callsign `ATN3337`, type B763, hex a4959a |
| Aircraft type and owner | FAA registry | Boeing 767-323, Mode S A4959A, Cargo Aircraft Management Inc. |
| Aircraft in our region | adsb.lol | airborne at 36,000 ft, 44.996N 122.087W, ground speed 450 kt, track 335 degrees |
| Interpreter output | on-screen | ACK blocks and "left FMC" unit name correct; the H1 text layout is not decoded |

The H1 text has an ARINC 622 shape (a seven character ground address `USADCXA`, the identifier `AT1`, then the registration and a hex-like
payload). It is not decoded and no meaning is claimed here.

## Analog ACARS window on 131.550 MHz, 10:22:39 to 11:07 UTC (45 minutes, one continuous run)

Because 136.975 MHz stayed silent through two earlier 45-minute windows, the unit was moved to the analog primary channel for a third
one. Result: 44 CRC-verified blocks (6 with a verified 1-2 bit repair), **all ground-station squitters from EUG**; no aircraft block was
heard. Screenshots: `analog-131550-list-1.png`, `analog-131550-list-2.png` (11:10 UTC; the counters at the end of the window read 40
messages, and 44 held by the time of the screenshot).

| Decoded on the device | Web source | Result |
|---|---|---|
| Squitter text `02XAEUGKEUG14407N12313WV136975/ARINC` read as station EUG (KEUG), ARINC, 44 deg 07 min N, 123 deg 13 min W | OurAirports `airports.csv`: KEUG, Eugene Airport, 44.124599, -123.211998 | the squitter position is 0.5 nm from the airport reference point (the squitter gives whole minutes): consistent |
| Station codes EUG / KEUG | OurAirports IATA `EUG`, ICAO `KEUG` | match |
| The squitter offers VDL2 on 136.975 MHz | live VDL2 traffic on 136.975 (SkyWest, Delta, N395CM on the same night) | consistent |
| Squitter rate | the 44 rows span 10:22 to 11:10 UTC, about one every 1 to 2 minutes | one station, steady |

## Continuous 45-minute VDL2 window with aircraft traffic: 11:58:08 to 12:43 UTC, matched live against adsb.lol and OpenSky

At about 12:00 UTC (early morning on the US West Coast) traffic picked up. One uninterrupted watcher run on 136.975 MHz (no other use of
the serial port) decoded **135 ACARS messages and 213 CRC-verified AVLC frames** (337 valid VDL2 headers; 64 frames carried no data and
were only counted). Decoder load stayed at 28.4 ms per 65 ms block, worst block 141 ms. The held list (newest 48) was read right after the
window; screenshots: `window-1158-1243-list.png` (LIVE list) and `window-1158-1243-details.png` (three message details).

Aircraft seen in the window and the live cross-check, run at 12:45:28 UTC (8 minutes after the last of these messages):

| Tail | Flight on our screen | adsb.lol live feed | Other source |
|---|---|---|---|
| N244SY | OO3708, label 32 positions 44.017N 123.203W at 12:35:05 and 44.013N 122.992W at 12:37:05 | `SKW3708`, E75L, hex a24082, now 44.030N 121.708W, 24,925 ft, 445 kt, track 089 | OpenSky: departed KEUG, first seen 12:32:41 UTC (our fixes are 2 and 4 minutes after take-off, 6 and 9 nm from the field) |
| N8862Q | WN1701 | `SWA1701`, B38M, hex ac36cc, FL330 | |
| N483WN | WN1899 | `SWA1899`, B737, hex a5f5bf, FL410 | |
| N298FR | MQ4048 | `ENY4048` (Envoy), E75L, hex a313bf, 27,925 ft | |

The ADS-B position of N244SY eight minutes after our second fix is 55 nm further east (about 400 kt average while climbing), consistent
with the track; the callsigns and types agree for all four aircraft.

Parsers that had only been host-tested were seen on the real screen in this window: label-32 position reports ("Position 44.013N
122.992W ..." from N244SY), H1 central fault display messages for OO3708 and MQ4048, and H1 flight data unit blocks for the Southwest
aircraft.

### New unread format from this window: Southwest H1 `#DFB` data blocks (WN1701, N8862Q)

Multi-block reports (`D17A` to `D17E` seen), ETB-terminated blocks of fixed-width numbers, for example:

`D17EWN1701#DFB13  -9210250327200445  87 -14  11  11  12 5` ... and `D17DWN1701#DFB  000-1300 1441 1456 2344 2348030003  90071 ...`

The interpreter names the unit and the report and shows the text; the columns are not decoded and no meaning is claimed. Decoding
this needs the blocks joined in order first (not done: the interpreter keeps no state across messages).

## Saved screens, 2026-10-29 17:00-17:05Z (unit up about 2 hours on the new firmware)

- `archive-pages/page-00.png` to `page-37.png`: all 38 pages (300 saved blocks) of the ARCHIVE screen, newest first. The newest saved block is 16:33:14Z and the oldest 16:20:54Z. The text is cut at the screen width, so long blocks end in an ellipsis.
- `live-list-1703/live-0.png` to `live-3.png`: the four pages of the 48 held LIVE messages at 17:03Z, where the list shows the interpreted summaries.
- Finding: the unit had counted 761 messages by 17:02Z, but the newest block in the archive was 28 minutes old. The SD archive was not keeping up with live traffic, or had stopped writing at its 300-block cap. Not yet diagnosed; this is why the live list was saved as well.
- The unit holds only the last 48 messages in RAM and 300 on the SD card, so everything earlier is only in the counters: 761 messages, 1262 verified frames, no gaps.
