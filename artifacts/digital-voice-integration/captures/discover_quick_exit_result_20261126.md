# DISCOVER with the 0.6 s quick exit, Tab5 COM17, 2026-11-26

One DISCOVER run (all picked bands except 900 MHz), about 15 survey passes, started after commit 830de982.

- The screen at the end of the run showed PASS 14, SIGNALS 128, FOUND 4, CANDIDATES 5.
- The persistent database grew; `RTL_DV MONITOR ALL` went from 9 to 15 channels. New DMR entries: 464.3625, 172.145 (CC13), 453.6, 171.2325, 162.720 (CC8), 456.1625 (CC13), 153.645 (CC8).
- Several of these were heard only once or a few times (HEARD 1 on the screen): they are leads, not confirmed systems. 153.645 MHz showed HEARD 444.
- Not done: the same survey with the old 1.5 s dwell, so a missed-system comparison is still open. The run ended when the serial helper reopened COM17 and reset the board.
