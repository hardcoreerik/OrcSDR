# LoRa / Meshtastic receive

OrcSDR's LoRa dashboard is a passive, receive-only SDR instrument. It never
transmits, pairs, controls mesh nodes, or invents mesh activity.

## Five-panel dashboard

- **Overview** shows the configured US LongFast receive profile, live RTL
  spectrum/waterfall, capture controls, and recent verified traffic.
- **Nodes** lists bounded sender records. A field is shown only when received;
  otherwise it is `—`. Links are never inferred from signal strength.
- **Traffic** retains local receive events. Payloads without an authorized key
  are marked `ENCRYPTED`, not shown as decoded text.
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

Native LongFast work is staged as IQ -> chirp/FEC/CRC -> Meshtastic frame parse
-> authorized decryption -> bounded snapshot. Until its recorded-IQ vectors
pass, the dashboard displays **PHY PENDING**. Unknown encrypted traffic is not
decrypted, recovered, or attributed.

## Local data and capture

`/orcsdr/lora_packets.csv` is optional local SD evidence. The 32-record RAM
queue and low-priority SD writer keep writes out of USB, IQ, and rendering
paths. Clearing the Traffic screen clears only the RAM list; it does not delete
the saved CSV. IQ capture remains explicit and is exported separately.

**Scan Band** is an explicit 902–928 MHz survey over fourteen 2 MHz spans. It
temporarily retunes the configured monitor, reports observed energy, then
restores the prior frequency. It is a survey, not reliable packet capture;
normal fixed-profile monitoring is the correct decode mode.

## Configuration

`/orcsdr/lora.cfg` is user-owned. The initial loader accepts `profile`,
`region`, `frequency_hz`, `sf`, `bandwidth_hz`, and a 64-hex-character
`authorized_receive_key`; invalid frequency/SF/BW values are ignored. Keys
remain local: no diagnostic, CSV export, screenshot, or API may include them.
Start from [`lora.cfg.example`](lora.cfg.example); never commit a populated
copy.
The masked Settings editor, channels, favorites, and map marks are still gated
on the native frame/decryption component's recorded-IQ validation.

## Validation

Before live claims, test recorded sync, CRC pass/fail, public LongFast,
authorized-key decode, encrypted-without-key, malformed frames, queue
saturation, profile change, and scan restoration. Hardware acceptance compares
metadata with the authorized Heltec V4 network and verifies no USB drops,
watchdogs, flicker, or sustained heap/task growth.
