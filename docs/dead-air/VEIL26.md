# VEIL-26 in Dead Air

VEIL-26 is an experimental, opt-in analysis engine inside Dead Air. It turns **measured numbers** (a tuned frequency and a
measured level) into **symbols** (a letter, a dictionary word, two gematria sums) with a fixed, deterministic recipe.

> Dead Air is allowed to be weird. Dead Air is not allowed to lie about what it measured.

## What it is, and what it is not

- It **is** a reproducible recipe. The same inputs and the same intention give the same symbols on any machine, and every
  saved record names its inputs so the result can be recomputed from the record alone.
- It **is not** a detector, a classifier, or a message. It never raises a Dead Air event, never changes the event detector,
  and never writes into the measured-event journal. A symbol carries no information about the source of a signal.
- It **cannot** receive Schumann resonances (about 7.83 Hz) or any other ELF signal: an RTL-SDR does not tune there. Genuine
  ELF analysis needs appropriate external sensing hardware. Nothing in VEIL-26 pretends otherwise.
- The screen separates **MEASURED** (frequency, level, source, time) from **SYMBOLIC / INTERPRETED** (letter, word, gematria,
  sigil state), and says so in words.

## Provenance and what is missing

The original package, `VEIL26_Esoteric_Frequency_Demodulator_v0.1.0.zip` (`VEIL26_README.md`, `esoteric_demod.py`), was
searched for on this machine and in the repository and **was not found**. This implementation follows the documented
recipe only. Consequences:

| Item | Status |
|---|---|
| State recurrence, seed derivation, quantization rules, A1Z26 and Pythagorean gematria | Implemented from the written description; checked against an independent Python implementation (hashlib, unbounded integers, round-half-even). **Not** checked against the original `esoteric_demod.py`. |
| The canonical 32-word dictionary | **Missing.** `kDictionary` is a clearly-labelled placeholder (`placeholder-0`). Every record carries `"dict":"placeholder-0"` and the config record says `"dict_canonical":false`, so placeholder results cannot be mistaken for canonical ones. Replace the table and `kDictionaryId` together. |
| Frame counter base | Assumed to be **0** for the first frame (Python `enumerate` order). Not stated in the description; confirm against the original. |
| Boundary of the log quantization "above 1 MHz" | Implemented as `f > 1 MHz` kilohertz, `1 kHz <= f <= 1 MHz` whole hertz, `f < 1 kHz` centihertz. Exactly 1 MHz is therefore hertz. Confirm against the original. |
| Gematria of a word | Plain sums are shown. A digital-root helper exists but is not displayed (the original's reduction rule is not known). |

## The algorithm

```
seed  = first 4 bytes, big-endian, of SHA-256("VEIL26-v1:" + intention)
state = seed

per frame (frame_index = 0, 1, 2, ...):
  state = ((state * 33) XOR (frequency_code * 131) XOR (level_code * 17) XOR frame_index) mod 2^32
  letter_number = 1 + (state mod 26)            -> letter A..Z
  word          = WORDS[(state / 26) mod 32]    (integer division)
```

- **Spectral convention** (`quantize_spectral`): `frequency_code = round(frequency_hz * 100)`,
  `level_code = round(rms_amplitude * 10000)`. Implemented and tested; not used by Dead Air yet (see "Staged").
- **Frequency-log convention** (`quantize_log`), used for Dead Air's RF observations: `frequency_code` is kilohertz above
  1 MHz, whole hertz from 1 kHz through 1 MHz, centihertz below 1 kHz; `level_code = round((level_db + 160) * 10)`. A level
  in dBFS is **not** an RMS amplitude, which is why it has its own convention.
- **Rounding** is Python's `round()` (halves to the even neighbour). Non-finite inputs quantize to 0, never undefined behaviour.
- **32-bit behaviour**: Python integers are unbounded and the result is reduced mod 2^32 at the end; the low 32 bits of a
  product, an XOR or a negative number do not depend on the high bits, so wrapping 64-bit arithmetic gives identical results,
  including for negative level codes (a level below -160 dB).
- **Gematria**: A1Z26 is A=1..Z=26; Pythagorean is A=1..I=9, J=1..R=9, S=1..Z=8. Letters only, case-insensitive.

Golden vectors (intention `hello`, seed `71c7425d`): 162.400 MHz at -31.7 dBFS gives state `abeb47ee`, letter `Y` (25), word
index 23; the next three frames are in `tests/dead_air_veil26_tests.cpp`.

## How it is connected to Dead Air

No second acquisition or detector exists. VEIL-26 reads what Dead Air already produces:

- **Periodic frames.** From the receiver observation Dead Air already makes (about 20 per second), at most **one frame per
  second**, and only from a *usable* observation: fresh level, settled (past the retune/gain/rate settling window), a tuned
  frequency, a finite level. Sweeps and control runs leave observations unsettled or paused, so no frames are made then.
- **Event frames.** Each RF rise and audio burst (a tuned frequency and a measured dBFS level) gets its own frame, tagged
  with the event's sequence number. Environment events and investigator marks have no such pair, so they are **not**
  interpreted; the overlay says so.
- **Breaks.** An unusable observation, a retune, or a receiver reconfiguration is a break: the state is **not** advanced
  (so the stream stays reproducible), the letter sequence shows `|`, and the break is counted. Skipped observations are counted.
- **Lifecycle.** Off at every session start. Turning it on starts a fresh stream (frame 0). Leaving Dead Air stops it and
  writes a summary. State lives in the PSRAM-resident session state; nothing is allocated while it is off beyond that.

The code: `dead_air_veil26.{hpp,cpp}` (pure engine and the `Stream` pacing policy), `dead_air_veil26_log.{hpp,cpp}` (record
formatting and the bounded queue), `dead_air_app.cpp` (feeding, the worker's file service), `dead_air_dashboard.cpp` (overlay).

## Using it

- **Overview > VEIL-26**, or **Events > select an event > VEIL-26**, opens the full-body overlay (the six tabs stay; a tab
  tap or CLOSE closes it).
- **ANALYSIS: OFF/ON** switches the stream. **INTENTION** cycles six presets (`DEAD AIR`, `ARE YOU THERE`, `LISTEN`,
  `ANYONE`, `OPEN CHANNEL`, `NO QUESTION`) and restarts the stream. A custom intention (up to 24 printable ASCII characters)
  can be set over authenticated serial.
- Opened from an event, it shows that event's measured row and, if the event was analysed, its symbols; otherwise it says
  why not. **LIVE STREAM** returns to the running stream.
- Serial: `RTL_DEAD_AIR VEIL` prints state and counters (read-only). Authenticated: `RTL_DEAD_AIR VEIL ON`, `OFF`,
  `INTENT <text>`.

## Optional runic display

[VEIL26_RUNES.md](VEIL26_RUNES.md) specifies the optional Elder Futhark display,
its exact state mapping, bias, names, record fields and limits. LATIN is the
default at every session start. The ALPHABET selector and authenticated
`RTL_DEAD_AIR VEIL ALPHABET LATIN|ELDER` change only display selection; they
never restart the canonical stream. Status reports the alphabet id.

## Records and reproducibility

Interpretations are written to their own file, `/orcsdr/dead_air/veil26_<session>.jsonl`, never to the measured-event
journal, and every line carries `"interpreted":true`.

- `veil26_config` on enabling or changing the intention/alphabet: algorithm, dictionary id and `dict_canonical:false`, quantization
  id, frame base, intention, seed, time, `rune_alphabet`.
- `veil26`: one per event frame and one periodic frame every 30 s. Fields include the algorithm and dictionary, trigger
  (`sample`/`event`), `event_seq`, `event_kind`, intention, seed, frame index, `t_ms`, `utc` (or `null` if the clock is not
  verified), the tuned frequency, the measured level and its source (`rf`/`audio`), the receiver configuration revision,
  `freq_code`, `level_code`, `state_in`, `state`, letter, word, both gematria sums,
  `rune_alphabet`, `rune_index`, and `rune_name` (the last two are null in Latin mode).
- `veil26_summary` when analysis stops or Dead Air closes: frames, breaks, skipped observations, records posted, dropped,
  written, and write failures.

A record is enough to recompute its symbols: `step_state(state_in, {freq_code, level_code}, frame)` must equal `state`
(this is a unit test). Because periodic frames are logged only every 30 s, a full stream replay needs the inputs of the
unlogged frames; each logged record is nevertheless independently verifiable through its own `state_in`.

### Bounds

- The UI task posts to a **16-slot, 640-byte-line single-producer queue** (about 10 KB). It never blocks and never waits for
  the worker; a full queue drops the line and counts it.
- The SD worker writes at most **4 lines per 100 ms pass**, syncing after a non-empty pass, so storage traffic is bounded by
  the queue and by the logging rate (events plus one line per 30 s).
- The first write or open failure ends the file for the session; queued lines are then discarded and counted, and live
  detection and the event journal are unaffected. The existing 4-slot note queue is not used.

## Performance and memory

Measured on an x86 host build (`-O2`), not on the device: a periodic observation costs about **6 ns** when it makes no frame
and the engine step is a few dozen integer operations; formatting one record takes about **0.45 us**. Scaled to a 400 MHz
RISC-V core these are roughly tens of times slower, still well under a microsecond per observation and about 20 us per record
(an estimate; not measured on the device).

Memory (all in the PSRAM-resident session state, no new internal-RAM statics): record queue 10,288 B, per-event ring
48 x 76 B = 3,648 B, engine/stream 112 B, plus 272 B in the `Snapshot`. About **14 KB of PSRAM**, constant, whether on or off.

VEIL-26 runs on the UI task, off every DSP and USB path; it takes no locks the receiver uses. It adds no DSP work. These
statements come from reading the code and from host timing; the USB IQ continuity of a flashed unit has not been measured.

## Testing

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui tests/dead_air_veil26_tests.cpp apps/orcsdr-tab5/ui/dead_air_veil26.cpp -o /tmp/veil26 && /tmp/veil26
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui tests/dead_air_veil26_log_tests.cpp apps/orcsdr-tab5/ui/dead_air_veil26_log.cpp apps/orcsdr-tab5/ui/dead_air_runes.cpp apps/orcsdr-tab5/ui/dead_air_veil26.cpp -o /tmp/veil26log && /tmp/veil26log
```

`dead_air_veil26_tests`: SHA-256 and the seed (including padding boundaries), Python rounding, both quantization conventions
and their boundaries, golden frames, negative codes, gematria, dictionary mapping, gaps and the letter tail, repeatability,
the stream pacing policy (one frame a second, retunes, unusable observations, events, restart).
`dead_air_veil26_log_tests`: record fields, recomputing a result from its saved record, escaping, worst-case line length,
queue overflow and wrap, draining to a failing writer. `dead_air_render_tests` covers the overlay, its entry points, event
inspection, and partial redraw (no full-screen fill on repeated frame updates).
The same tests run in `.github/workflows/dead-air-host-tests.yml`.

## Staged, not done

- **Audio-frequency / spectral processing.** The spectral convention is implemented, but Dead Air does not yet measure an
  audio-frequency peak: the audio meter gives a level, and the spectrum bins are RF display data around the tuned
  centre. Feeding either as an audio-frequency measurement, or the tuned centre as a detected peak, would be dishonest, so
  nothing does. A real audio-peak input needs a bounded FFT of the demodulated audio on the worker side; not built.
- **The canonical dictionary and verification against the original prototype** (see Provenance).
- **Hardware validation.** Nothing here has been flashed or run on a unit.
