# Dead Air: measured evidence and optional esoteric interpretation

Dead Air is OrcSDR's receive-only RF investigation dashboard. It can be weird without claiming to detect ghosts. **The measured values are real radio/sensor observations; esoteric interpretations are explicitly separate.** The maintainer confirms it operates with an M5Stack Unit ENV III attached to a Tab5.

## What is measured

The six-tab dashboard provides observed receiver activity and signal levels, frequency-local relative RF changes/events, demodulated audio observations and optional event-linked clips, event/session logging, and environmental conditions when the ENV III is attached. Measured RF level is relative **dBFS**, not calibrated absolute dBm. It reports actual timestamps or uptime with clock-quality caveats, never invented historical data.

**ENV III (Tab5 Port A):** temperature in °C, relative humidity in %, pressure in hPa. The SHT30 and QMP6988 readings are validated by the sensor logic; missing, warming-up, stale and disconnected channels do not masquerade as current readings. Environmental trend views summarize accepted minute samples, with gaps rather than invented interpolated readings. Hardware operation is maintainer-confirmed; full sensor accuracy calibration and every cable/power configuration are not independently certified.

## VEIL-26: optional symbolic analysis

VEIL-26 is an **implemented opt-in deterministic transformation** of the receiver's real measured tuning frequency and signal level. It computes a symbolic letter/word, numerology/gematria results and an optional rune display under documented rules. These are **INTERPRETED**, not additional measurements, received messages, or evidence of supernatural causes. The engine does not create RF observations, claim simultaneous frequency coverage, decrypt traffic or change the independent event detector.

VEIL-26 writes separately marked symbolic records rather than modifying the measured event journal. The original VEIL-26 prototype and canonical dictionary were not available for direct equivalence validation; the current implementation marks its dictionary as a placeholder and has independent host tests. Exact VEIL-26 on-device UI/soak acceptance has not been independently logged in the current document.

The design principle is simple: **Dead Air is allowed to be weird. Dead Air is not allowed to lie about what it measured.**

## What this does not claim

No empirical validation for paranormal detection or symbolic interpretation, no assertion that the cause of an RF or environmental change is an entity, no ELF/Schumann resonance reception with a normal RTL-SDR, and no universal RF/sensor accuracy or long-duration reliability guarantee.

For the technical implementation, see the [Dead Air development log](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/dead-air/CHANGELOG.md), the [VEIL-26 specification](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/dead-air/VEIL26.md), and the [maintainer field-acceptance record](https://github.com/hardcoreerik/OrcSDR/blob/main/docs/MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md).
