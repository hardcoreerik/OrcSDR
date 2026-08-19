# Feature status

| Feature | Status | Notes |
|---|---|---|
| FM receive, stereo audio, presets, and health | Implemented | Live hardware verified |
| FM RDS decoding | Implemented | Sequential A–B–C–D groups, confirmed PS, voted PTY, Radio Text. Needs a 260 kHz FM filter. This dongle uses a +13 kHz LO bias that is not shown as the channel. |
| P25 control and clear voice following | Experimental | Encrypted voice is not decoded |
| ADS-B 1090 dashboard and aircraft database | Experimental | Live coverage depends on antenna, location, and valid position messages |
| LoRa receive, packet views, and capture | Experimental | No transmit path |
| AM, WX, CB, and Browse tools | Experimental | Shared radio/scope/capture foundation |
| Global on-device Settings | Implemented | Wi-Fi and Companion remain optional |
| Offline personalized map packs | Deferred | Import/status shell precedes the builder workflow |
| Bluetooth speaker audio | Unavailable | Tab5 C6 supports BLE, not ordinary Classic Bluetooth A2DP output |
| Companion phone integration | Deferred | On-device operation never depends on it |
| LAN web console + Android TV viewer | Experimental | Opt-in Mission Control display (not a Tab5 clone); no TLS. Sideload `apps/orcsdr-tv` on Android 9 TV. Unplug the PC flash/JTAG USB cable after flashing — that cable, not general power, is the bench brownout trigger. |
