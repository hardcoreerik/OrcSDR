# OrcControl v2

All integers are little endian. Every ESP-NOW packet is exactly 48 bytes; bytes 42–43 are reserved zero. CRC-32/IEEE covers bytes 0–43, with the result at bytes 44–47. Transport integrity and application CRC reject corruption; neither authenticates the sender. Version 1 receivers will reject version 2 packets.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | magic `ORDL` |
| 4 | 1 | version, currently 2 |
| 5 | 1 | message type |
| 6 | 1 | role: Dial 1, receiver 2 |
| 7 | 1 | reserved |
| 8 | 4 | sender boot-session ID (MAC is the paired identity) |
| 12 | 4 | sequence |
| 16 | 4 | acknowledged sequence |
| 20 | 4 | signed command value |
| 24 | 4 | actual/requested frequency in integer Hz |
| 28 | 4 | tuning step in integer Hz |
| 32 | 2 | gain in 0.1 dB units |
| 34 | 2 | squelch setting |
| 36 | 2 | signal in dBm |
| 38 | 1 | mode |
| 39 | 1 | volume |
| 40 | 1 | flags: bit 0 means signal is measured and valid |
| 41 | 1 | authoritative dashboard ID in `RADIO_STATE` |
| 42 | 2 | reserved |
| 44 | 4 | CRC-32 |

Types: 1 HELLO, 2 PAIR_REQUEST, 3 PAIR_ACK, 4 HEARTBEAT, 5 TUNE_RELATIVE, 6 TUNE_ABSOLUTE, 7 SET_STEP, 8 SET_MODE, 9 SET_GAIN, 10 SET_VOLUME, 11 SET_SQUELCH, 12 REQUEST_STATE, 13 RADIO_STATE, 14 ERROR, 15 SET_DASHBOARD. `SET_DASHBOARD.value` is a dashboard ID. `RADIO_STATE.dashboard` is the receiver's current screen, including changes made on the Tab5. IDs follow the current Tab5 registry: 0 Home, 1 FM, 2 P25, 3 ADS-B, 4 Shortwave, 5 Weather, 6 CB, 7 LoRa, 8 Airband, 9 Marine, 10 Satellite, 12 Settings, 13 RF Lab, 14 Wi-Fi Analysis, 15 POCSAG, 16 AM. ID 11 Utilities is a menu and is rejected. `TUNE_RELATIVE` value is signed integer Hz. `TUNE_ABSOLUTE` is integer Hz. `RADIO_STATE.ack` identifies the command applied; resends use the original sequence. The test receiver rejects duplicate sequence numbers and resends the same authoritative state. Frequency is clamped to 24 kHz–1.766 GHz in the test receiver, matching the Tab5's documented wide tuner bounds; per-mode limits remain a Tab5 integration decision.

The Dial sends one command at a time, retries twice at 650 ms, and marks the link lost after three seconds without receiver traffic. It displays actual received state. Pairing and normal traffic are unencrypted in phase 1. The receiver stub accepts commands only from its saved MAC and only allows replacing that MAC during an explicit pairing window. MAC filtering and CRC are not cryptographic security. Production integration should use ESP-NOW encrypted unicast peers with provisioned keys, or an authenticated pairing exchange, before accepting radio commands.
