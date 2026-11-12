# AIS (marine vessel reports)

The AIS dashboard receives the VHF marine **Automatic Identification System**: ships, base stations and aids to navigation that broadcast their identity and position on 161.975 MHz (AIS 1) and 162.025 MHz (AIS 2). OrcSDR is **receive-only** here; it never transmits AIS.

!!! warning "Not a navigation tool"
    The dashboard shows what the receiver decoded. It is not certified for navigation or collision avoidance, and it makes no collision claims.

## Evidence level: read this first

- **Checked:** deterministic synthetic AIS recordings, replayed over a private network connection. On the host, the independent reference decoder recovers all 43 transmitted packets with matching fields. On the Tab5 the replay produced CRC-valid packets on both channels at the expected rate (about 43 per pass) with no lost samples. The Tab5 comparison is by packet counts, not a byte-for-byte comparison of every packet. Every screenshot below comes from a replay and carries the orange **TEST REPLAY DATA – NOT LIVE RF** banner. The positions are invented, laid out off the Oregon coast only so the map has something to show.
- **Not yet proven:** reception of real ships. The lab receiver has so far heard only noise on these channels, so sensitivity and sustained use are unmeasured. See the [AIS integration checkpoint](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/AIS_INTEGRATION_CHECKPOINT.md) for the exact record.

## Before you open it

1. Use an antenna suited to about 162 MHz (a marine VHF or an air-band/VHF whip). AIS is line of sight: expect tens of nautical miles over open water and much less from inland or behind hills.
2. Choose your receiver: a USB RTL-SDR, or an `rtl_tcp` server on your network (Settings > RTL_TCP). AIS asks for one 162.000 MHz / 240 kS/s stream and takes both channels from it.
3. Set your receiver location in Settings so the map centres on you. Without offline maps (see [Offline maps](../offline-maps.md)) the map area shows only the targets and range scale.
4. Open **ALL DASHBOARDS** and choose **AIS** (it is on page 2). Opening AIS takes over the shared 162 MHz stream, so it closes other dashboards that use the same stream (ACARS, SSTV, FT8, Dead Air).

## The seven pages

The row of buttons along the bottom switches pages. The header shows Home, the speaker (see [shared controls](../shared-controls.md)) and Settings.

### Live Map

![AIS Live Map with synthetic vessels off the Oregon coast](../images/ais/ais_1_map.png)

Vessels, base stations and aids to navigation on an offline map, with live counters (targets, messages, CRC-valid packets per channel). Use **−** and **+** to change the range: 10, 25, 50 or 100 nautical miles (it opens at 25). Tap a target dot to select it.

### Targets

![AIS Targets list and station details](../images/ais/ais_2_targets.png)

Up to the 32 most recent stations, newest first, with class, type and age. Tap a row for details: MMSI, call sign, navigation status, position, course and speed, destination and when it was last heard.

### Messages

![AIS Messages journal with packet inspector](../images/ais/ais_3_messages.png)

A rolling journal of the last 16 valid packets (older ones scroll off; this page is not a complete log): age, channel, message type, station ID and a decoded summary. Select a row to see the raw packet hex in the inspector. Use **UP** and **DOWN** to scroll.

### Protocol

![AIS Protocol page listing message types](../images/ais/ais_4_protocol.png)

All 28 AIS message types from ITU-R M.1371-6, with how many valid packets of the selected type are in the journal. Only some types are decoded to fields (position reports, base stations, static and voyage data, Class B, aids to navigation); the rest show header information only.

### Signal

![AIS Signal and reception diagnostics](../images/ais/ais_5_signal.png)

Reception diagnostics: IQ sample rate, CRC-valid packets per channel, CRC failures, dropped IQ blocks and the transport (USB or RTL_TCP). **CRC FAIL counts every noise burst that looked like a packet**, so a large number on a quiet band is normal. The two spectrum panels say **NO MEASURED FFT DATA** because this build does not compute them; it will not draw a made-up graph.

### Events

![AIS Events page: collection not available](../images/ais/ais_6_events.png)

Safety and activity events are **not available in this build**; the page says so rather than showing an empty list. The Messages page shows the most recent 16 packets.

### Setup

![AIS Setup page: display only](../images/ais/ais_7_setup.png)

Shows the fixed configuration: both channels on, the input source, and what is not available yet (long range channels 75/76 need a retune or a second radio; NMEA output and the message journal file are not implemented). The settings are display only; taps change nothing.

## Test replay mode

For development and these screenshots, the firmware can label a session as a test replay: every page then shows **TEST REPLAY DATA – NOT LIVE RF**. The label is a manual switch set by an authenticated serial command (see the [serial reference](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/API_SERIAL_CLI.md)); it does not detect where the data came from. It is independent of the input source, so check that it is on before trusting replay screenshots and never treat its absence as proof that data is live. It never switches itself on.

## Troubleshooting

| What you see | What it usually means |
|---|---|
| TARGETS 0, MESSAGES 0 | Normal on a quiet band or with no ships in range. Check the Signal page: `RECEIVING` with a rising CRC FAIL count means the receiver works and no valid packets have arrived. |
| Receiver shown as disconnected | No receiver, or the stream was taken by another dashboard. Reopen AIS. |
| Map shows no land | Offline maps are not installed or the receiver location is unset. |
| Long range channels unavailable | Not implemented; they sit at 156.8 MHz, outside the single 162 MHz stream. |
