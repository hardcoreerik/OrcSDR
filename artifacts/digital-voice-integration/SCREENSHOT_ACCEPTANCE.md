# Device screenshot record (NOT yet owner-approved)

Firmware: local branch local/digital-voice-integration, built with native ESP-IDF 5.5.4, flashed to COM17
(M5Stack Tab5, ESP32-P4, app-only flash; settings/SD preserved). Path: `UI_DOC_SHOW digital_voice.<view> live`
then `UI_CAPTURE` -> framebuffer BMP -> lossless PNG (captures/). No receiver attached at capture time, so
screens show the truthful "RECEIVER DISCONNECTED / scanner not connected" state; they are live-state, not demo.

| View | File | SHA-256 of BMP (device-reported) |
|---|---|---|
| SCAN | captures/dv_scan.png | 303e7d96885578d3b00e31cd34167c079489cad40101e285cfe091b2cc7f58f4 (two captures identical) |
| LIVE | captures/dv_live.png | 77ab54face7736cc61092d285f364c4d719fd5453ea8aa63e1f937c0489a604c |
| LAST HEARD | captures/dv_heard.png | 61a71a18956cf26b8708a6e5f25a81e47bc1633e9b58f5cdca150549eee6b2bf |
| SYSTEMS | captures/dv_systems.png | c2a29e1ce99f2c6d2a3324cda727f6bea92d214f9badff55af694d21d75f3da2 |
| SIGNAL | captures/dv_signal.png | 78d600af12befe4e620a23999e847fa108126938e647560edbf3a83262a27ed4 |
| SETUP | captures/dv_setup.png | 85532544eb0fde667fae022bc73d8c594bebd631afeb65acabd7f880f6b8dd38 |

Reproducibility: SCAN captured twice -> identical hash (pixel-identical). First SCAN capture after flash showed Home
(first-show race right after reboot); discarded, not used. Other five captured once; repeat before approval.
Not covered: populated states (need decoder events), Analysis-first density, scanning/hold states, DV-01..22 manifest cases.
Stress: 3x open / LIVE / SETUP / Home cycles over serial, device stayed responsive. Heap/PSRAM not yet measured.
