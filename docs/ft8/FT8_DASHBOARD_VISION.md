# FT8 dashboard vision

## Experience

The FT8 dashboard should feel like OrcSDR, not like a desktop weak-signal program squeezed onto a tablet.

A new user should be able to open FT8 and press **HUNT**. OrcSDR should do the radio-specific work: visit conventional FT8 dial frequencies, observe correctly aligned receive slots, decide whether activity is merely energy or credible FT8 structure, attempt decoding, rank bands, and offer the best result.

An experienced user can directly select a band and stay there.

## 1280 x 720 layout rules

The FT8 dashboard follows existing OrcSDR Tab5 conventions:

- 1280 x 720 landscape.
- Existing shared top header and home/navigation affordances.
- Dark background, cyan borders, green active/success state, amber caution/pending state.
- Large touch targets suitable for the 5-inch Tab5.
- M5GFX/M5Unified rendering only.
- No dashboard-specific rendering code added to `main.cpp`.

## LIVE

LIVE is the primary receive screen.

It should show:

- selected amateur band and conventional FT8 dial frequency;
- USB receive mode;
- accurate UTC status;
- decoder state;
- gain state;
- a 15-second slot countdown/progress indicator;
- a waterfall or FT8-focused activity surface;
- candidate count only when the decoder actually provides it;
- the previous slot's valid decode count;
- the newest decoded messages.

State language should be human-readable:

`WAITING FOR UTC` -> `LISTENING` -> `DECODING` -> `READY`

Errors and missing decoder bindings remain explicit.

## DECODES

The decode table is an evidence view, not a contact logger.

Columns:

- UTC
- SNR
- DT
- audio offset / DF
- message class
- decoded text
- Maidenhead locator when present

The UI may visually emphasize CQ messages, but it must preserve the decoded text.

## MAP

MAP is offline-first.

A station is plotted only when a successfully decoded FT8 message contains a valid Maidenhead locator. The UI describes these as **station-reported locators**.

The map should later reuse OrcSDR's offline map infrastructure when practical. A simple world/grid fallback remains valid when no map pack is installed.

## HUNTER

Hunter is a core FT8 feature.

### User modes

**FAST HUNT**

- Visit selected conventional FT8 frequencies.
- Align observation to FT8 slots.
- Use low-cost evidence first.
- Rank each band as QUIET, ENERGY, FT8 SIGNATURE, or DECODED.
- Prefer speed over deep decoding.

**DECODE HUNT**

- Stay for multiple slots.
- Run the full decoder.
- Rank bands using valid decode count, candidate quality, and receive strength.
- Offer **LISTEN HERE** on the best band.

### Example result

| Band | Dial | State | Slot result |
|---|---:|---|---:|
| 40 m | 7.074 MHz | FT8 SIGNATURE | 0 valid |
| 30 m | 10.136 MHz | QUIET | 0 |
| 20 m | 14.074 MHz | DECODED | 14 |
| 17 m | 18.100 MHz | ENERGY | 0 |

Hunter must never convert ENERGY into an FT8 claim.

### Scanner lifecycle

```text
idle
 -> tune
 -> settle
 -> wait for slot boundary
 -> observe
 -> detect energy
 -> detect FT8 signature
 -> optionally decode
 -> score band
 -> next band OR lock best band
```

The user can stop Hunter at any point.

## HEARD

HEARD summarizes callsigns proven by decoded FT8 messages.

Initial release should not depend on QRZ, HamQTH, PSK Reporter, or any network service. Internet enrichment can be explored later as an optional overlay and must never replace the decoded local evidence.

## SETUP

The setup screen owns:

- Hunter mode;
- included/excluded bands;
- slots per band;
- gain mode;
- UTC source/status;
- decoder backend/status;
- decode depth/performance mode if later exposed;
- session clear/export/logging options when implemented.

Advanced controls should not be required for normal use.
