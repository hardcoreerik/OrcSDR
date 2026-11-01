# IF-state candidate hardware attempt, 2026-11-01

Driver 8912d2075252ed587ba45312d7b91af6fb46b0ae, one approved build/COM39
flash. Image SHA-256 9417af098d04f0ae9e75c44d10c654ea00d5ae8ed4cfb0ccf1366607f3439e16,
5,444,688 bytes. Managed driver checked against source. Esptool verified
written hashes and intentionally reset tablet. Existing HAL/M5GFX retained.
COM17/COM8 unchanged. Previous image retained locally.

User returned XTR to COM39 with tinySA/30 dB attenuator unchanged.
First attempt failed public-health synchronization before source output ON;
cleanup OFF/pause acknowledged. A fresh health query succeeded. Attempt2
completed baseline and several gain captures, then COM39 serial read failed
(ClearCommError). Cleanup also found tinySA COM7 missing. Both transport
failures prevent full-run acceptance. Do not describe this as a completed
matrix, AUTO pass or corrected repeatability. COM7 reappeared; separate
output OFF and pause commands succeeded and were acknowledged after failure.
No more builds or flashes were attempted.

Partial IQ files were SHA-256 verified. See partial-results.json for integrated
+/-4-bin Hann tone powers. The ascending 14->16.5 dip remains, so restoring
IF baseline has not solved that symptom. Same-gain return/AUTO acceptance
is incomplete. Candidate remains unqualified; do not merge as an RF fix.
Raw/partial files and console exceptions retained in ignored build directories.
This interruption does not establish whether firmware or host USB caused it.

A third shorter attempt stopped at public-health synchronization before RF
output ON. Subsequent standalone health query succeeded at uptime 7,157,343 ms;
no reset was observed between the two successful standalone health queries.
Fresh output OFF and pause acknowledgments were received again. Automated
captures stopped. The health reader also treats AUTH_REQUIRED as terminal,
so stale console/auth lines must be distinguished from actual missing health
or a USB fault before another capture attempt.

Four completed signal captures integrated powers: 14=-26.984,
16.5=-27.642, 19=-24.160, returning 16.5=-27.754 dBFS.
The 14->16.5 dip is -0.658 dB. The 16.5 return differs -0.112 dB.
The return to 14 was not retrieved completely. Full-repeat/AUTO gates open.
