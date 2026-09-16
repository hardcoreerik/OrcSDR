# LoRa / Meshtastic receive

OrcSDR's LoRa dashboard is a passive, receive-only SDR instrument. It never
transmits, pairs, controls mesh nodes, or invents mesh activity.

## Five-panel dashboard

- **Overview** shows the configured regional LongFast receive profile, live RTL
  spectrum/waterfall, capture controls, and recent verified traffic.
- **Nodes** lists bounded sender records. A field is shown only when received;
  otherwise it is `—`. Links are never inferred from signal strength.
- **Traffic** retains local receive events. Payloads without an authorized key
  are marked `ENCRYPTED`, not shown as decoded text. Each event always keeps
  monotonic receive uptime for age/order and optionally snapshots trusted UTC;
  synchronizing later never rewrites older events.
- **Map** is an M5GFX topology grid, not an online map. It plots only verified
  received coordinates and links only when protocol evidence supplies them.
- **RF Health** reports receiver rate, USB/consumer drops, capture/log state,
  activity, and native decoder readiness.

The shared **Home** button returns to Home; the gear opens Global Settings.
LoRa deliberately has no audio controls or alert tones.

## Current receive boundary

The Tab5 captures CU8 IQ at 960 kS/s, displays the RF view, and maintains a
bounded local event/log path. Existing `LORA_PACKET` serial input remains a
regression bridge for known, externally verified Meshtastic decode records.
It is not described as native decoding.

The native LongFast decoder is implemented in `lora_native_decoder` and
connected to the capture task: IQ -> chirp/FEC/CRC -> Meshtastic frame parse
-> authorized decryption -> bounded snapshot. Decoder self-check and
initialization determine readiness; the dashboard displays **PHY PENDING**
when the native decoder is not ready. Readiness does not certify live RF or
complete replay coverage. Unknown encrypted traffic is not decrypted,
recovered, or attributed.

## Local data and capture

`/orcsdr/lora_packets.csv` is optional local SD evidence. The 32-record RAM
queue and low-priority SD writer keep writes out of USB, IQ, and rendering
paths. Clearing the Traffic screen clears only the RAM list; it does not delete
the saved CSV. **Export Log** on Nodes writes only the bounded recent display
list (at most eight decoded events) to a new `/orcsdr/lora_NNN.csv`; it does not
enable continuous logging. CSV retains `uptime_ms` and adds `wallclock_valid`,
`wallclock_source`, and `received_utc`. IQ capture remains explicit and is
exported separately.

**Scan Band** samples up to fourteen evenly spaced LongFast slots in the selected
region. It retunes, waits 750 ms for each measurement, ranks the three strongest
relative-energy readings on Overview, then restores the prior frequency. The live
meter and survey results are dBFS, not calibrated dBm. It is a survey, not reliable
packet capture; normal fixed-profile monitoring is the correct decode mode.

The Overview visualization is independently implemented for the Tab5. Its
fixed-scale heat palette, averaged spectrum, channel markers, and bounded time
history follow common SDR presentation concepts reviewed in
[Gqrx](https://github.com/gqrx-sdr/gqrx) and
[SDR++](https://github.com/AlexandreRouma/SDRPlusPlus), both GPL-licensed; no
source, palette table, or rendering code was copied.

## Configuration

`/orcsdr/lora.cfg` is user-owned. The initial loader accepts `profile`,
`region`, `frequency_hz`, `sf`, `bandwidth_hz`, and a 64-hex-character
`authorized_receive_key`; invalid frequency/SF/BW values are ignored. Keys
remain local: no diagnostic, CSV export, screenshot, or API may include them.
Start from [`lora.cfg.example`](lora.cfg.example); never commit a populated
copy.

The dashboard **Channels** picker selects and saves a Meshtastic region and a
1-based frequency slot. Frequency is derived from that pair and restored after
reboot. The picker contains the 24 current geographic plans compatible with the
native decoder's LongFast SF11/BW250 receive path. Meshtastic's 2.4 GHz,
EU Lite/Narrow, and licensed amateur profiles are intentionally not offered:
the RTL-SDR cannot tune 2.4 GHz and the native decoder currently accepts only
250 kHz LoRa. Region definitions and slot calculation track the official
[Meshtastic radio implementation](https://github.com/meshtastic/firmware/blob/develop/src/mesh/RadioInterface.cpp).

## Validation

Before live claims, test recorded sync, CRC pass/fail, public LongFast,
authorized-key decode, encrypted-without-key, malformed frames, queue
saturation, profile change, and scan restoration. Hardware acceptance compares
metadata with the authorized Heltec V4 network and verifies no USB drops,
watchdogs, flicker, or sustained heap/task growth.

The automatic capture retains 250 ms before the energy trigger and four seconds
total at 960 kS/s. This covers a maximum-size LongFast packet whose airtime can
exceed 3.2 seconds. `RTL_IQ_RETRIEVE_*` plus `tools/decode_orciq.py` remains the
offline evidence path; it does not yet execute the exact device-native decoder.
