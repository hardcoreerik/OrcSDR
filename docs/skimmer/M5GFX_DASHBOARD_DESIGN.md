# OrcSDR CW/RTTY Skimmer — M5GFX UI-first dashboard

Target: M5Stack Tab5, 1280x720, M5Unified/M5GFX native drawing. This is a **firmware dashboard**, not a web page, image mockup or decoded-radio feature. Source files: `apps/orcsdr-tab5/ui/skimmer_dashboard.*`.

## UX contract

- Tabs: LIVE, STATIONS, DECODES, SETUP. Tab bar and shared navigation retain OrcSDR conventions.
- LIVE: band/context strip, CW/RTTY/BOTH filter, spectrum/waterfall review area, touch-select station list and selected message.
- STATIONS: frequency, modulation, station label, strength/confidence.
- DECODES: message list with station and mode; decoded text only when provided by demo or a future verified backend.
- SETUP: explicit demo toggle and filter. Controls not backed by RF are not represented as operational.
- No simulated detections in non-demo mode. Demo data are fictional and clearly marked **UI PREVIEW - NO LIVE RF**.

## Architectural contract

- Only UI code calls M5.Display. DSP and radio integrations must publish a bounded Snapshot with monotonic signal/spectrum revisions.
- No new rendering code in main.cpp. Main owns only lifecycle, screen routing and touch dispatch.
- Separate UI lifecycle from radio activity; opening the preview must not retune or make RF measurements.
- No DX-cluster output, logging, actual CW/RTTY decodes, click-to-tune or fake signal evidence in this stage.
- Value updates paint bounded regions; full screen clearing is allowed only for screen-entry/tab-layout changes.
- Real decoding is a later stage, subject to signal-detection, callsign confidence and RF evidence policies.

## Host-side regression

From apps/orcsdr-tab5:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Itests/skimmer_stubs -Iui \
  ui/skimmer_dashboard.cpp tests/skimmer_dashboard_tests.cpp \
  -o /tmp/skimmer-dashboard-tests
/tmp/skimmer-dashboard-tests
```

The test checks DEMO labeling, touch tabs, filters, disabling previews, decoder data arrival and dirty-region updates without new full-screen clears. Hardware compile/flash, touch acceptance, RF continuity, PSRAM cost, and visible flicker remain **unverified until run on Tab5**.
