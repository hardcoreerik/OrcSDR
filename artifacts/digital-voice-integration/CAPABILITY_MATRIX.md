# Capability matrix (what is proven, where)

Sources: docs/dmr/TRACKING.md, NXDN/DV docs on the DV branch, plus host suites I re-ran on 866a380d.
"Host" = passes on PC from IQ or hard bits. Nothing below runs on a Tab5 yet except P25 (mainline).

| Protocol | Frontend | Metadata | Real-air IQ | On Tab5 | Voice |
|---|---|---|---|---|---|
| DMR Tier II | native 4FSK (48 kS/s) | sync, CACH, CC, LC src/TG, CSBK CRC; slot only when evidenced | 2 pinned positives + 2 negatives, 40/40 (CI 44028198169) | NO (device probe only) | none |
| NXDN48/96 | native 4FSK, 48 kS/s | FSW, LICH, SACCH CRC RAN (RAN 0 valid) | 3 pinned clips: NXDN48 voice 12 SACCH-valid, NXDN96 voice 19, RCCH 0 valid (CI 45118929168) | NO | none; VCH candidates are not speech |
| D-STAR | none (hard bits only) | DV header callsigns/routing | none | NO | none |
| YSF | none (hard bits only) | FICH | none | NO | none |
| P25 Ph1 | mainline native | full, existing | existing | YES | clear voice works |
| P25 Ph2 | mainline | partial | partial | partial | unsupported |

Gaps that block "scanner": no protocol-detect/AUTO classifier, no IQ attachment from the
radio session, no event bridge into `digital_voice_ui::Snapshot`, Type-C/D grants undecoded.
