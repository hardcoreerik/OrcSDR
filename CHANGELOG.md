# Changelog

## Unreleased

### Weather Dashboard Foundation

- Added the dedicated five-tab offline-first Weather dashboard.
- Added source/freshness modeling and seven-channel NOAA Weather Radio channel handling.
- Opening Weather no longer implies a receiver retune; NOAA Listen/Scan are explicit foreground actions.
- Reused the existing WX/NFM receive path and shared scan engine rather than adding a parallel DSP/audio path.
- Added Weather SD snapshot/report format and default-disabled online policy plumbing.
- Added host regression coverage for Weather model, NOAA sequencing, runtime behavior, screen ownership/layout, report formatting, and receiver borrowing.
- Weather Hunter/SAME, personal weather sensors, radiosondes, and satellite weather remain out of scope for this phase.
