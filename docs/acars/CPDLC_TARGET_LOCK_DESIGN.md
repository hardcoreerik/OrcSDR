# CPDLC in the existing ACARS dashboard and Aircraft Target Lock

Status: **feature-branch first slice; NOT a complete CPDLC message-element decoder, NOT an RF scanner.**  
Branch: `feature/acars-cpdlc-target-lock`  
Scope: receive-only, reuse the established ACARS/VDL2 path, no new tuner owner.

## Current verified baseline

- Live ACARS MSK + VDL2 D8PSK/AVLC are already implemented. No new "CPDLC frequency" is required.
- The ACARS H1 interpreter already recognises the ARINC 622 wrapper `/BA ... .AT1....`, validates its specific CRC, and exposes uninterpreted application bytes.
- The FANS-1/A `AT1` IMI denotes CPDLC (ICAO training slide source: https://www.icao.int/sites/default/files/sp-files/SAM/Documents/2010/SURAUTOSEM/17%20Arinc_CPDLC%20ADSC.pdf).
- This branch marks validated `AT1` envelopes **CPDLC ... (payload not decoded)** in the same LIVE interpretation and detail screen. A valid ARINC 622 envelope is *not* proof that its encoded clearance/response has been interpreted.
- Other VDL2 AVLC data frames are preserved as raw `AV` records. `AV`'s 28-bit link identifier is **not** automatically an aircraft registration.

## Aircraft Target Lock: first slice

A user can select a message -> detail -> TARGET LOCK, or select an aircraft on the AIRCRAFT tab -> TARGET LOCK.
The modal displays the exact selected data-link identifier, supplied flight identifier (if any), and count of matching held ACARS records.
It does not create a timer, issue a receiver retune, query ADS-B or claim verified correlation. The modal consumes touch events until CLOSE, so tapping a channel/tab behind it cannot retune the radio.
Raw `AV` messages cannot open the modal because source address != verified registration.
All UI state remains in the existing `Controller`; the existing renderer owns the popup, with no new full-frame allocation and no work from the DSP callback.
Do not call this *continuous tracking*, *multi-frequency lock* or *all-channel monitoring* until those features actually exist.

## Planned application decoding layers

1. Capture pinned real `AT1` examples already in the ACARS archive and verify the exact ARINC 622 envelope CRC against the existing interpreter.
2. Add a bounded, allocation-free FANS-1/A CPDLC payload decoder (message ID, references, response attributes, codewords, arguments and cardinality). Test against `libacars` on known real/synthetic examples; for unknown or incomplete payloads retain raw and say undecoded.
3. Add ATN B1/B2 transport/session and APDU parsing for raw AVLC frames, independently benchmarked against `dumpvdl2`. ATN frames cannot be treated as FANS-1/A just because both carry CPDLC.
4. Feed verified semantic events back into the **same ACARS LIVE** list and detail card. No extra screen or invented frequency. Preserve exact bytes and protocol/CRC evidence.
5. Only then offer a "CPDLC only" filter. It must distinguish validated CPDLC transport from complete decoded messages.

## Driver retune investigation (do not bypass it)

The separate `hardcoreerik/esp-rtl-sdr` repo currently pauses/drains bulk URBs, applies tuning over EP0, performs RF frontend updates and resets the IQ FIFO before resuming. Its source records ~188–190 ms hot retunes, of which ~171 ms were in `run_profile_tune()` on the measured configuration, with dozens of USB control transfers. These are **measurements for the recorded configuration**, not guaranteed for every dongle, sample rate, ESP32 board or RF band.

A tuner datasheet's ~5 ms PLL locking specification does **not** imply 5 ms end-to-end retunes. No register optimization is accepted until confirmed by two or more repeatable RF/USB captures and device safety checks.

Required driver work on a separately reviewed driver branch:
- Profile each stage with per-record and endpoint timings: pause/drain, repeater, tuner PLL, V4/V4L RF route/filter, FIFO reset, resume, first valid post-retune IQ.
- Compare two same-band hops (e.g. 136.975 <-> 136.800 MHz) and one cross-band hop (136.975 <-> 1090 MHz) at the exact same sample rate; also classify rate changes, because the present driver requires stop/start to change sample rate during streaming.
- Investigate redundant control writes (cache already exists) and unavoidable read/PLL-lock requests. Never assume EP0 with outstanding bulk URBs is safe on ESP32-P4 simply because a PC USB stack permits concurrent transfers.
- Before permitting any optimized shortcut: check PLL lock, RF route, correct signal center, checksum-valid ACARS/VDL2/ADS-B, acquisition epochs, stale-block rejection, stream discontinuity, USB faults and recovery on V4/V4L/V3c/Nooelec independently.
- Save host and hardware log files in the directory from which the test command was run, including hardware, commit SHA, frequencies, dwell, and per-phase timings.

## Future receive-mode UX

**Single dongle:** Target Lock scan would use a consented, bounded scanning plan with measured hop/settle/dwell times; clearly indicate that the current ACARS/VDL2 channel is paused while scanning. A fast retune can reduce missed windows but cannot cover VHF and 1090 MHz at the same instant.

**Two or more dongles:** Pin one receiver to 1090 MHz ADS-B and another to VHF ACARS/VDL2. Reuse the existing device-specific stream ownership and attach read-only consumers; don't spin up a second USB owner.

**Correlation:** A tail, flight ID, ICAO24, AVLC link address, station identifier and frequency are distinct kinds of identity. Only label a match VERIFIED with direct cross-reference evidence and temporal consistency; otherwise label it CANDIDATE or UNMATCHED. An aircraft can be on any supported frequency regardless of its current ADS-B position; do not infer where its CPDLC session moved without evidence.

## Acceptance plan

- Host interpreter: known seven checksum-valid AT1 records display `CPDLC ... (payload not decoded)`; CRC-corrupted records cannot masquerade as verified CPDLC.
- Host UI: from message detail and aircraft selection, open/close the modal, assert no `tune_requested` or tab navigation while visible. `AV` raw link IDs cannot open target lock from message detail.
- Renderer regression: open/close, receive additional messages, navigate after close; assert values change without whole-screen clears for incremental data updates; explicitly exercise header overlay coexistence.
- Firmware: build ESP-IDF with no new hot-path allocations. Only run physical tests with explicit device permission, recording UI flicker, 250 kS/s VDL2 continuity, and receiver/tuner metrics.
- Driver: separate RF-verified performance and safety acceptance; no claim that a fast-retune improvement is already implemented.

## How to run/test

From a local checkout on this branch (Linux/WSL):

```bash
# Save logs in the invocation directory as well as the script's existing build/acars location.
LOG="$PWD/acars-cpdlc-target-lock-tests.log"
SANITIZE=1 bash tools/test-acars-ui.sh 2>&1 | tee "$LOG"
```

Expected checks: ACARS model/controller touch tests and ARINC 622 interpreter tests, plus original renderer and receiver suite. Report actual pass/fail and the generated log, not an assumed pass. Do not flash any device without explicit operator authorization.
