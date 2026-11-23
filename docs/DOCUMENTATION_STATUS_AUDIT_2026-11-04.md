# Documentation freshness audit — 2026-11-04

## Purpose

Remove outdated maturity labels while retaining reproducible, accurate implementation and RF evidence for both OrcSDR and the independent `esp-rtl-sdr` driver.

## Scope and method

- Inventoried **326 Markdown documents** in OrcSDR `main` and **99 Markdown documents** in `esp-rtl-sdr` `master` through the GitHub repository trees.
- Searched both repositories for **experimental**, **provisional**, **not verified**, **not implemented**, receiver model status and historical version references. Reviewed current-facing entry pages, user guide, architecture, project-truth/status files, profile docs and dated physical evidence against the actual integrated source/release descriptions.
- Historical release notes, research plans, and lab observations were **not bulk rewritten**. Their dated statements are important audit evidence and should not be retroactively presented as original passing results.
- This review updates documentation only. It is not an RF retest, firmware build, calibration certificate or claim that every markdown file was manually fact-checked line by line.

## Concrete stale claims corrected in OrcSDR

| Issue found | Correction |
|---|---|
| Current project summary still claimed a 2026-10-02 `v0.3.0-beta.2` preparation snapshot | Updated to 2026-11-04 current-source audit; [v0.5.0-beta.1](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.5.0-beta.1) is the latest versioned published release observed |
| Project status and roadmap said no native Tab5 CI build existed | Documented current path-filtered SSTV Tab5 build and gated nightly ESP-IDF builder; every-PR compile coverage remains incomplete |
| V3c and V4L characterized as experimental, and one page said V4L was not verified | Named V4/V3c/V4L/Nooelec V5 as fully supported receivers; model RF calibration remains evidence-scoped |
| Shortwave described as generic NFM Browse with no AM/SSB | Documented Shortwave Explorer AM/USB/LSB, AUTO, clarifier, filters and memories present in main and `v0.5.0-beta.1` release notes |
| Airband described as generic NFM | Documented dedicated 118–136.975 MHz aviation AM tuning/scanner; physical RF acceptance still pending |
| FT8 labelled experimental despite live four-dongle acceptance | Recorded supported FT8 reception with dated 7.074 MHz evidence; JS8 stays separate |
| SSTV title was experimental; claimed only Robot36 had real receiver acceptance | Updated to conducted Robot36/PD120/Scottie S1 image/SD results on V3c/V4/V4L/Nooelec V5 (2026-10-26); 11 other modes remain host-test-only |
| ACARS omitted from summary despite actual off-air receive | Added ACARS decoder/dashboard entry with two off-air blocks from 2026-10-15; VDL2 is separate |
| LoRa marked experimental as a whole | Availability/hardware traffic surfaced; packet-loss/coverage caveats retained |
| UI/Wi-Fi/catalog/companion availability obscured by experimental modifiers | Classified implemented functionality separately from security, RF and device-coverage limits |
| OrcDial integration called experimental without naming its remaining blocker | Kept the real C6 no-update/transport integration caveat instead |

## Independent `esp-rtl-sdr` documentation corrections

- The driver implements Blog V4, V4L, V3c and Nooelec SMArt V5; some pages still described V3c as a provisional stream or Nooelec P4 reception as entirely pending despite recorded hardware and OrcSDR FT8 acceptance.
- E4000/XTR has implemented identity/probing, PLL/zero-IF tuning, IQ streaming, 14-step manual gain and tuner AUTO, with initial dated Tab5 FM/RDS reception. Full-band/long-soak/absolute-gain acceptance is not established.
- Driver `PROJECT_TRUTH.md`, `README.md`, `SCOPE.md`, `PROFILES.md`, capability matrix, roadmap and E4000 notes were inconsistent. The separate driver documentation PR corrects status without calling every RF band verified.
- E4000 canonical documentation moves to `docs/E4000_PROFILE.md`; the old experimental filename remains only as a compatibility pointer for historical links.

## Labels that should remain restrictive

- **P25 Phase II AMBE+2 voice**: absent; Phase II signaling progress is not voice support.
- **JS8 / VDL2 / additional decoders**: do not inherit FT8 or ACARS RF acceptance.
- **Higher-rate production audio (3.2 MS/s), unverified RF performance or LF frequency edges**: no claim of completion.
- **Generic RTL dongles / ESP32-S2 or S3 Full-Speed hosts**: not qualified by the supported P4 model matrix.
- **LAN console**: trusted LAN only without TLS/authentication.
- **Nooelec XTR/E4000 in OrcSDR**: driver implemented, app tuning bounds and dashboard capability gating remain in progress.
- **Dead Air**: measured RF/sensor data must remain distinct from symbolic interpretation; do not portray it as paranormal detection.

## Recommended continuing process

Keep [status terminology](STATUS_TERMINOLOGY.md) and [current feature availability](user-guide/feature-status.md) close to the user documentation. Require a concrete dated artifact or demonstrated source path before each public claim, and update these pages alongside feature changes. Retain dated reports as evidence rather than editing their historical outcomes.
