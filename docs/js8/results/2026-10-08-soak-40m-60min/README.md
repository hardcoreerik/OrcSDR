# JS8 Normal soak, 40 m, 2026-10-08

Headless JS8 Normal receive on the Tab5 (V4 dongle, GA-800 indoor antenna), dial 7.078 MHz, 61 minutes, 240 slots, driven by `tools/js8-soak.py`.

- `device.log` - every ORC_JS8_RT / ORC_JS8_RT_FRAME line with host timestamps (per-frame sync hits, initial/final syndrome, BP iterations, OSD order, corrections, CRC, raw payload, rendered text).
- `analysis.txt` - `tools/js8-soak-analyze.py` output (grid CN84, 1500 km "should hear" radius).
- `psk-senders.json` - distinct senders PSK Reporter listed for JS8 on 7.000-7.300 MHz during the run (spot count, best SNR, frequency range). The seven raw XML samples (about 10 MB) are kept outside the repository.

Result: 73 frames reached the soft decoder, 53 passed parity and CRC, 51 messages from 7 stations, all 7 also spotted on PSK Reporter, no decoded station absent from PSK Reporter.
76 stations were reported by receivers within 1500 km, so 69 were not decoded; only directed HEARTBEAT SNR frames are rendered so far, and an indoor antenna hears fewer stations than a typical reporting receiver.
Mean slot time on the P4 was about 8.5 s of 15 s (refinement cost; see the speed-up notes). Stations: K8IMT, KD7WPQ, WD5EED, W7SUA, W7YSB, KS1DMD, WB7TSQ.

Evidence limit: PSK Reporter establishes activity only. Matching a sender does not verify the decoded text or rule out false decodes. This soak did not replay audio derived from its exact raw IQ through standard JS8Call, so it is operational evidence, not a reference-paired reconstruction fixture.
