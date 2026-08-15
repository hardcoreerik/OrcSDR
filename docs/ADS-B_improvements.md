# ADS-B Improvement Roadmap

## Current implemented baseline

OrcSDR receives 1090 MHz Mode-S / ADS-B using the RTL-SDR V4 driver at
2.048 MS/s. The live dashboard currently provides:

- ICAO 24-bit address and callsign;
- barometric altitude, groundspeed, heading, and vertical rate when present;
- global CPR position after a valid even/odd pair;
- relative signal level, message rate, aircraft count, and stale-track handling;
- local FAA SD-index enrichment for registration, type, and owner.

FAA metadata is lookup enrichment only. A registration shown beside a live
ICAO address comes from the local index; it is not transmitted by ADS-B.

## Next decoder fields

Prioritize fields that are broadcast by the received aircraft and can be
validated with recorded frames before live-air use:

1. Emitter category, including rotorcraft, glider, light aircraft, and heavy
   aircraft classifications.
2. Emergency and squawk information, when transmitted.
3. Target state/status: selected altitude, autopilot, VNAV, approach, and TCAS
   state when present.
4. Position integrity and accuracy: NIC, NACp, SIL, and related validity
   indicators. UI must show unknown rather than imply precision.
5. Altitude and velocity source/type distinctions, including GNSS versus
   barometric altitude and ground versus airspeed.
6. Surface/on-ground state for airport and heliport activity.
7. Carefully scoped Mode-S DF20/DF21 and BDS support, after separate vector
   coverage and without presenting inferred values as broadcast facts.

## Receive-quality work

Do not expose unmeasured tuning controls. First establish a five-minute live
1090 MHz baseline with:

- effective sample rate;
- USB and ADS-B decoder queue drops;
- preamble, frame, CRC, and DF17 counts;
- aircraft count and message rate;
- strongest relative signal.

Only compare one gain strategy at a time. A fixed-gain option is appropriate
only after the current driver exposes and validates it. Frequency-error/PPM
calibration is justified only by measured error against a known reference.
Bias-tee and direct-sampling remain absent until their driver paths are
hardware-measured; they are not ADS-B defaults.

## Explicit limits

- A single passive receiver cannot generate MLAT positions.
- Route, origin, destination, and airline schedule information are not normal
  ADS-B broadcasts; any future external-data integration must be optional.
- 978 MHz UAT is a separate receiver/decoder effort, not a 1090 MHz setting.
- Never fabricate unknown fields or present encrypted/unverified information as
  decoded aircraft truth.

## Acceptance gates

Each decoder addition requires known-frame tests, malformed-frame tests, and
recorded-IQ replay before a live-air comparison. Each receive adjustment must
meet the baseline without causing USB drops, task/heap growth, watchdogs, or
dashboard flicker.
