# Live MY BANDS sweep, Tab5 COM17, captured 2026-11-25 (sweep at channel 4326-4438 of 7759, pass 0, no antenna LNA, bias off)

Sync-level candidates (NOT verified; the screen says "Candidates are never identities"; seen once each):
- NXDN48 455.200000 MHz (first/last 03:24:09)
- NXDN48 454.912500 MHz (03:23:40)
- NXDN48 454.362500 MHz (03:22:39)
- NXDN96 447.650000 MHz (03:10:38)
Times are the Tab5 clock. RAN and channel type unknown for all four. Only one channel in this sweep was verified.

Verified database at capture time: 12 DMR systems, 3 talkgroups, 5 units (saved on the SD card), all on 151-153 MHz:
151.195 CC13; 152.1575 CC13, CC4, CC5, CC11, CC12; 152.685 CC8, CC13; others on pages 2-3 (see sweep_systems_p2/p3.png).
Many of those CC entries have 1-2 hits and are likely false detections (see the false-systems item).

Screenshots: sweep_scan.png, sweep_scan_after.png, sweep_lastheard.png, sweep_systems_p1/p2/p3.png.
Not preserved by the firmware: sync-only candidates live in RAM only and are lost on restart.

## Update: sweep at channel 4974 of 7759 (sweep_463.png)
- VERIFIED at integrity level: NXDN48 463.250000 MHz, RAN 1, RTCH / TRAFFIC, first 03:38:46, last 03:38:52, observed 1 time. Integrity validated, not identity (no unit or group IDs). Sweep counters: VERIFIED 2, SYNC 7, NEW 2, pass 0.
- New sync-only candidates: NXDN96 461.7125 MHz (03:35:59), NXDN48 461.2875 MHz (03:35:13), NXDN96 460.750 MHz (03:34:14).

## Final state when the sweep was stopped by the owner (channel 5259 of 7759, about 68%, pass 0, never completed)
- NXDN48 463.250000 MHz, RAN 1 is in the persistent database: HEARD 67 (n48_ok=67 validated frames), last 03:38:52. Stronger than the "observed 1 time" scan-row suggested. Still integrity level: no unit or group IDs decoded.
- Database saved to the SD card at the stop: 13 systems, 3 talkgroups, 5 units (12 DMR on 151-153 MHz plus this NXDN48 system).
- Sync-only candidates seen (RAM only, not saved): NXDN48 455.2000, 454.9125, 454.3625, 461.2875 MHz; NXDN96 447.650, 461.7125, 460.750 MHz.
- Decoder counters at stop: n48_frames=72 n48_ok=67, n96_frames=52 n96_ok=0, dmr_acq=0, dmr_ev=17, loss=0, gaps=0.
- Channels 5260-7759 of the plan were NOT swept. Files: final_scan.png, final_stopped.png, final_systems_p1..p4.png, final_status.txt.
