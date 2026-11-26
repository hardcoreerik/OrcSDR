# NXDN implementation state — 2026-11-22

| Feature | NXDN48 | NXDN96 |
|---|---|---|
| 48-kHz IQ → 4FSK dibits (2400 / 4800 sym/s) | Implemented; synthetic tested | Implemented; synthetic tested |
| 20-bit FSW, PN9 LICH | Implemented; synthetic tested | Implemented; synthetic tested |
| K5/CRC6 SACCH with 6-bit RAN | Implemented; synthetic tested | Implemented; synthetic tested |
| Raw 72-bit VCH candidate extraction | Implemented; unqualified | Implemented; unqualified |
| Verified 49-bit AMBE+2/Golay frames | Not implemented | Not implemented |
| FACCH/group/source identities | Not implemented | Not implemented |
| CAC / Type-C and Type-D trunk tracking | Not implemented | Not implemented |
| Licensed clear voice | Not implemented | Not implemented |
| Real-air physical layer (3 REA clips, replayed 2026-11-25) | 12 FSW/LICH, 12 SACCH CRC6-valid, RAN 7 | 19 FSW/LICH, 19 SACCH CRC6-valid, RAN 7; RCCH 19 frames at 192 dibits, -664.9 Hz |
| Opaque SACCH 72-bit reassembly (no field layout) | Implemented; real air 1 message | Implemented; real air 4 messages |
| 11.8 s / 15.3 s WaveShark captures | Not acquired | Not acquired |
| ESP32-P4 / live scanner / M5GFX | Not connected | Not connected |

All published evidence must distinguish a LICH candidate, FEC/CRC-valid RAN, truly validated identities and actual voice. Synthetic round-trips do not prove interoperability.