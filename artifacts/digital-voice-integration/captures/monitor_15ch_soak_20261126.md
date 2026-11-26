# MONITOR over 15 discovered channels, 12 min, Tab5 COM17, 2026-11-26

Tool: `monitor_probe.py` (status sampled about every 2 s, so seconds are approximate). 75 holds in 12 min.

| channel (MHz) | holds | approx s on channel |
|---|---|---|
| 152.1575, 153.120, 151.195, 153.645, 152.6825, 152.685 | 10 to 12 each | 70 to 80 each |
| 464.3625 (new from DISCOVER) | 5 | 48 |
| 463.400 | 4 | 38 |
| 463.250 (NXDN48), 451.9125, 172.145, 453.600, 171.2325, 162.720, 456.1625 | 0 | 22 to 28 each |

- Six VHF channels produce control traffic every visit and take about 6.7 s each (6 s control-only hold plus settle), roughly 40 s of a round of about 75 s.
- 464.3625 MHz, found by the quick-exit DISCOVER run, holds on real DMR traffic; it is a real system, not a one-off.
- The seven zero-hold channels are quiet in this window; one visit each is about 2 s, which is the cheap part of the round.
- No voice was decoded in this soak.
