# OrcSDR Weather Dashboard Roadmap

This directory summarizes the implementation split for the offline-first Weather dashboard. The normative product design is:

- [`../superpowers/specs/2026-10-06-weather-dashboard-design.md`](../superpowers/specs/2026-10-06-weather-dashboard-design.md)

Execution is intentionally split so the first usable dashboard does not depend on every RF decoder succeeding:

1. [`Weather Dashboard Foundation`](../superpowers/plans/2026-10-06-weather-dashboard-foundation.md)
   - dedicated five-tab 1280x720 Weather UI;
   - no receiver retune merely by opening Weather;
   - source/freshness model;
   - existing NOAA NFM Listen + seven-channel scan on demand;
   - shared offline map/location/catalog integration;
   - SD reports/history;
   - Internet policy plumbing with **disabled** as the default.
2. [`Weather Hunter`](../superpowers/plans/2026-10-06-weather-hunter.md)
   - non-preemptive RF job scheduler;
   - sequential Weather Hunter;
   - explicit NOAA SAME RF Watch;
   - fixture-backed personal weather-sensor decoding;
   - 400.15-405.99 MHz radiosonde discovery and Balloon Hunter.
3. [`Satellite Weather`](../superpowers/plans/2026-10-06-weather-satellite.md)
   - cached/offline pass prediction;
   - event-driven 137 MHz Satellite Hunter;
   - experimental LRPT decode behind a measured Tab5 resource gate;
   - direct-RF imagery/report integration.

## Product rules

- **Offline is the default.** Direct RF, local/cached data, OrcMaps, and SD history are the primary path.
- **Online is enrichment.** It is opt-in and must never be required to open or use the core dashboard.
- **One tuner, one RF job.** Cached values show their age; Weather never pretends that NOAA, ISM sensors, radiosondes, and satellites are monitored simultaneously.
- **User intent wins.** An explicit Weather Listen/Scan/Hunter action is a foreground receiver request and may intentionally replace the previous RF job. Scheduled/opportunistic Weather work may borrow only an idle receiver and must abort rather than steal it.
- **Measured data stays distinct from interpretation.** Signal hits are not identities; candidates are not decoded devices; predicted passes are not receptions.
- **Reports are auditable.** SD bundles keep structured observations, RF events, source provenance, optional screenshots/maps, and file hashes.

## Initial RF coverage

| Target | Range / channels | Initial behavior |
| --- | --- | --- |
| U.S. NOAA Weather Radio | 162.400, .425, .450, .475, .500, .525, .550 MHz | Listen or sequential scan on demand |
| Personal weather sensors | regional profile; North America starts with 315, 433.92, and 902-928/915 MHz | Hunter only; decoder support is fixture-gated |
| Radiosondes | 400.15-405.99 MHz | Hunter discovery; explicit Balloon Hunter after validated lock |
| Weather satellites | target catalog near 137 MHz | pass-driven only; no blind periodic polling |

The implementation must reuse OrcSDR's existing receiver location, `offline_map`, signed `noaa_weather` catalog, WX/NFM DSP, `radio_session`, `time_service`, and `orcsdr_storage` rather than duplicating those services.
