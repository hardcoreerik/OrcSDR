# OrcSDR: radio freedom, without the laptop

For the published [v0.3.0-beta.1 release](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.3.0-beta.1). Device screenshots below come from a PNG-capable build based on later `main` source; see [[Screenshot Catalog]] for provenance.

OrcSDR turns an M5Stack Tab5 and a supported RTL-SDR into a portable, receive-only radio. Start with a suitable antenna and a familiar FM station.

## Start

1. Follow [[Getting Started]] for the published package and setup.
2. Finish startup checks, acknowledge OK, and open FM Radio from Home.
3. Tune a known station, unmute and adjust header volume.
4. Read [[Gain and Shared Controls]] before changing RF gain.

Home provides spectrum and waterfall, tuning controls, recent dashboards, Settings and the GAIN panel. Tap **ALL DASHBOARDS** to see the full menu shown below; use **NEXT** to reach its second page. A shortcut does not guarantee complete decoding support on every band.

## Use a dashboard

| Dashboard | Purpose |
| --- | --- |
| [[FM Radio]] | Broadcast FM, presets, stereo and RDS |
| [[AM Radio]] | Broadcast AM, station finding and spectrum |
| [[FT8 Reception]] | Receive-only FT8, setup and dated four-receiver reception evidence |
| [[Shortwave]] | HF tuning, schedules, hunt, memories and logbook; experimental reception |
| [[CB Radio]] | 40-channel AM/USB/LSB listening and scanning |
| [[Weather and Other Bands]] | NOAA weather; Airband, Marine and Satellite limitations |
| [[P25 Radio]] | Phase I monitoring and clear voice; Phase II audio unavailable |
| [[ADS-B Aircraft]] | Live 1090 MHz broadcasts |
| [[LoRa Monitor]] | Experimental LoRa/Meshtastic reception |
| [[POCSAG Pager Monitor]] | Messages, IDs, signal health and session history |
| [[Wi-Fi Survey]] | Access-point survey through the Tab5 wireless coprocessor |
| [[RF Lab]] | Controls, measurements, records and visualizers |
| [[Settings]] | Device settings, firmware status, data, maps and connectivity |

See [[RTL-SDR Driver]], [[Troubleshooting]] and [[Screenshot Catalog]]. An empty live screen is normal when no signal is present; a populated illustration is not reception evidence.

## Solve a problem

If the receiver is missing, audio is silent or a dashboard stays empty, start with [[Troubleshooting]]. Include your release, receiver, antenna and the relevant screen when [reporting an issue](https://github.com/hardcoreerik/OrcSDR/issues/new).

## Technical reference and verification

This guide describes the [published beta.1 package](https://github.com/hardcoreerik/OrcSDR/releases/tag/v0.3.0-beta.1). [[Screenshot Catalog]] records the separate capture build and image hashes. [PR #48](https://github.com/hardcoreerik/OrcSDR/pull/48) tracks the Home layout change; [PR #115](https://github.com/hardcoreerik/OrcSDR/pull/115) tracks the release. A merged PR establishes source history, not device or RF acceptance. See the [project status record](https://github.com/hardcoreerik/OrcSDR/blob/main/PROJECT_STATUS.md) for current verification boundaries.

Reviewers can start with [[Documentation and Evidence]], then [clone the wiki Markdown](https://github.com/hardcoreerik/OrcSDR.wiki.git), follow its page links and inspect the cited PRs and permanent commits. The source repository is separate; cloning OrcSDR alone does not include these wiki pages.

## Screens on the Tab5

These are actual 1280 × 720 Tab5 captures from 29 September 2026, using PNG-capable source `897c60a`. They show the interface, not proof of reception. **Live** and **demo** refer to capture mode; the source is newer than the published release binary. See [[Screenshot Catalog]] for file hashes and provenance.

### Home (live, idle)

![Actual Tab5 Home screen with recent dashboards, spectrum, tuning controls and Gain button](images/v0.3.0-beta.1/home.png)

The selected mode was idle when this picture was taken, so the spectrum and waterfall are blank.

### Home gain panel (live)

![Actual Tab5 Home screen with the receiver gain panel open](images/v0.3.0-beta.1/home-gain.png)

This example shows tuner AGC, manual gain and RTL AGC for the attached RTL V4. Available controls change with the receiver and band; see [[Gain and Shared Controls]].

### Dashboard menu · page 1 (live)

![Dashboard menu · page 1 view on Tab5, live capture](images/v0.3.0-beta.1/dashboard-menu.png)

### Dashboard menu · page 2 (live)

![Dashboard menu · page 2 view on Tab5, live capture](images/v0.3.0-beta.1/dashboard-menu-page-2.png)
