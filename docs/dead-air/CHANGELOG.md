# Dead Air Changelog

Changes to the Dead Air dashboard. Newest first. The design contract and the development handoff are in `DEVELOPMENT_HANDOFF.md`.

## Unreleased - Overview RF strip, tappable Events, scrub, control run, band map, orb, session card

Branch `claude/dead-air-dashboard-review-8c2b12`. Host tests and the native ESP-IDF build only so far; not flashed, so none of it has been seen on the panel.

### Added (Environment trend charts)

- **Charts:** the Environment tab now plots temperature, humidity and pressure as three trend lines, newest at the right edge. Each point is the mean of the accepted sensor samples in one minute, kept for the last 24 hours (`dead_air_envhistory`, 1440 minutes x 3 channels x int16 = 8.4 KB, inside the PSRAM-resident session state, so no new boot-time static).
- **Honest gaps:** a minute with no valid reading is a gap and the line breaks there; nothing is interpolated or carried over, and an unplugged module leaves a visible hole. The vertical range fits the data but is never narrower than 2 C, 10 %RH or 4 hPa, so a quiet room does not look like a trend. The axis labels are the real ends of the range.
- **Controls:** 1 H / 6 H / 24 H buttons choose the window (until you pick one, the smallest window that holds everything recorded so far). Tapping a chart places a cursor across all three and shows each value and how long ago that moment was; tapping near it again clears it.
- **Silence and gaps:** the window advances with the clock, not only when a sample arrives, so a sensor outage shows as gaps rather than the last value; a 24 H column that contains a missing minute is itself a gap, so a short outage cannot hide inside an averaged column; a refused (out-of-range) sample never moves the window; elapsed time is tracked in 64 bits so a session can run past the 49.7-day clock wrap. The dashboard is handed chart-only changes at most every 10 s.
- **Limits:** the history lives in memory for the session and starts empty each time Dead Air opens; it is not written to the SD card yet. DEVICE MOTION moved to the left panel.
- **Validation:** host tests pass; the native ESP-IDF 5.5.4 build is clean; flashed to COM39 and the Environment tab opened over serial. The panel itself (layout, colours, flicker) has **not** been looked at.
- **Review fix:** the chart cursor now clears when the automatic window changes on its own (for example 1 H to 6 H as history grows). The value-only update test stays at the full-screen threshold because the host canvas stub has no pixel buffer and always repaints the body directly.
- Tests: `dead_air_envhistory_tests` (minute averaging, scaling, gaps, late and bad samples, the 24-hour wrap, column summaries) and a new renderer block (lines per chart, gap breaks, window buttons, cursor, no full-screen clear on repeated updates).

### Added (VEIL-26 optional Elder Futhark display)

- ALPHABET cycles LATIN / ELDER FUTHARK beside INTENTION. The symbolic panel
  shows a line-drawn rune and its conventional name beside the unchanged Latin
  letter, word and gematria. Event inspection uses the saved state with the
  currently selected alphabet. Every session starts in LATIN.
- Authenticated `RTL_DEAD_AIR VEIL ALPHABET LATIN|ELDER` shares the UI setter;
  status and replies report the alphabet id. Selection never restarts analysis.
- Interpretation records add alphabet id, rune index and ASCII name. The rule,
  modulo bias, historical caveats and omissions are in `VEIL26_RUNES.md`.
- Host tests cover independent integer vectors, recomputation from records,
  glyph validity, bounds at several sizes, canonical stream independence,
  runic event inspection and repeated partial redraws. Device rendering, touch
  and serial operation are unverified; no hardware was flashed or accessed.

### Added (VEIL-26 / ESOTERICA: an optional symbolic analysis engine)

Branch `claude/dead-air-veil26`. Full design, algorithm, provenance gaps and limits: `VEIL26.md`.

- **What it does:** turns the measured tuned frequency and level into a letter, a dictionary word and two gematria sums with a fixed, reproducible recipe (SHA-256 seed from an intention, a 32-bit state recurrence, Python rounding). Off at every session start; opt-in from **Overview > VEIL-26** or **Events > VEIL-26**.
- **What it does not do:** it is not a detector. It adds no acquisition and no event detector, never raises or edits a Dead Air event, and its output is labelled SYMBOLIC / INTERPRETED beside a MEASURED panel. The overlay says in words that the symbols are not messages or detections, and that an RTL-SDR cannot receive ELF/Schumann signals.
- **Inputs:** at most one frame a second from a usable (valid, settled, tuned) receiver observation, and one frame per RF rise or audio burst. Retunes, unusable observations and sweeps are counted breaks, never fabricated frames.
- **Records:** separate file `veil26_<session>.jsonl` (config, per-event and every-30-s frame records, summary), always `"interpreted":true`, each recomputable from its own fields. A bounded 16-slot queue and at most four lines per 100 ms keep storage traffic bounded; a write failure ends the file for the session without touching the event journal.
- **UI:** a full-body overlay in the existing M5GFX style; the six tabs are unchanged. Repeated frames repaint only what changed (tested: no full-screen fill). Event inspection shows an event's symbols or says why it was not analysed.
- **Serial:** `RTL_DEAD_AIR VEIL` (state and counters); authenticated `RTL_DEAD_AIR VEIL ON|OFF|INTENT <text>`.
- **Known gaps:** the original VEIL-26 v0.1.0 archive was not available, so the 32-word dictionary is a **placeholder** (`placeholder-0`, flagged in every record) and the engine is checked against an independent implementation of the documented recipe, not the original code. The frame-counter base (0) and the exact 1 MHz boundary are assumptions. Audio-frequency/spectral input is staged, not built. Not flashed; host tests and the native build only.
- Tests: `dead_air_veil26_tests`, `dead_air_veil26_log_tests`, and an overlay block in `dead_air_render_tests`.

### Added (ENV III sensor: the Environment tab now has a data path)

- **ENV III driver:** until now the Environment tab and the Overview card had their fields but nothing filled them, so a module plugged into Port A showed "MODULE NOT CONNECTED" however it was wired. Dead Air now reads the Unit ENV III on the Tab5's Port A (SDA GPIO53, SCL GPIO54): temperature and humidity from the SHT30 (0x44, CRC-8 checked) and pressure from the QMP6988 (0x70, chip id checked, compensation as in the manufacturer's reference code).
- **Port A power:** the Tab5 switches the Port A 5 V rail through an IO-expander pin that nothing in the app turned on. Opening Dead Air now enables that rail (Port A only; the USB-A rail is untouched) and leaves it on.
- **What counts as a reading:** a channel shows a value only after a checked reading (CRC / chip id / a plausible range: -40..85 C, 300..1100 hPa). A module that is present but has not produced one yet shows as warming up; a reading older than 10 s shows STALE; a channel that fails five times in a row is dropped, its value cleared, and re-probed every 2 s. Unplugging the module therefore returns the tab to "UNAVAILABLE", and plugging it back in recovers without leaving the screen. Nothing is simulated.
- **Polling:** the sensors are read once a second from the UI tick, only while Dead Air is open, with a non-blocking state machine (the SHT30 conversion is started, then read 20 ms later). The Environment and Overview screens repaint when a displayed value changes at the printed decimal, and when a reading goes stale.
- **Serial:** `RTL_DEAD_AIR ENV` prints bus/sensor presence, the latest values and their age, and counters (reads, CRC errors, bus errors, attaches). It is only meaningful while Dead Air is open.
- **Environment events:** temperature, humidity and pressure now log events (`temperature_change`, `humidity_change`, `pressure_change`) when a reading leaves its own recent baseline, in either direction, and stays away. `dead_air_envevents` is pure logic: the baseline is learned over the first 15 samples and follows slow drift (about 20 s time constant, so an hour-long drift never fires); a departure must hold for 5 s (3 s for pressure) so a one-sample blip is ignored; one event per departure, re-armed when the reading returns within half the limit; a departure that lasts five minutes is learned as the new normal. Limits (Quiet / Standard / Sensitive): temperature 1.5 / 1.0 / 0.5 C, humidity 6 / 4 / 2 %RH, pressure 0.8 / 0.5 / 0.25 hPa, using the same sensitivity as the RF detector. A sensor gap over 5 s, an unplugged module or a non-finite value resets that channel, so an unplug can never look like an event. An event is a measured departure and says nothing about its cause: a breath on the sensor and a draught both log one. Event rows and the detail panel show value, baseline, change and limit. Not done for these events: the orb burst, the chime and the session card's counts (they still cover RF, audio and marks), and nothing starts a measurement, scan or clip when one is logged yet.
- **Not done:** device motion (`motion_g`) is still not filled.
- Tests: `dead_air_env_tests` (SHT30 CRC and decoding against the datasheet check value; QMP6988 compensation against three results from the manufacturer's reference code, also checked bit-for-bit against it over two million random inputs during development; attach, cadence, CRC and bus failures, drop and recovery, wrong chip id, implausible pressure, forget). Not tested on hardware: the Port A rail, the bus itself and the module's real readings.

### Added (second batch: making it worth watching)

- **The orb:** a small animated cue on the Overview (120 px) and in the header of every tab (52 px). Idle, it breathes and brightens with how close the live RF level is to the event limit (dim and still when nothing is measured). When an event is logged it bursts: rings and sparks whose number scales with the measured rise in dB, in red (RF), orange (audio) or violet (a mark you made). It is a cue that a logged measurement happened, never a claim about its cause. Drawn from PSRAM sprites at about 15 frames a second during a burst.
- **Scrub:** SCRUB in the Events detail shows 10 s before to 10 s after the event, RF and audio, sampled every 0.25 s, with the baseline and limit lines, a movable cursor with a readout, and PLAY CLIP: the clip is hash-checked, played, and a cursor follows it (only for the event the clip is anchored to, its first). Traces are kept for the 48 events in memory (about 16 KB of PSRAM).
- **Control run:** CONTROL RUN on the Events tab runs the same RF detector on all three sensitivities for five minutes on a setup you declare to be empty (antenna removed, dummy load), and reports false alarms and false alarms per hour for each. Automatic detection pauses during the run; gaps do not count as measured time; a rate is shown only after a minute of measurement. The result is written to the journal as a `control_run` record.
- **Band map:** a sweep records the peak level on every channel it dwells on and shows it as bars where the waterfall would be (LIVE / MAP chip to switch). Unmeasured channels are dashes; tapping a bar tunes to that channel.
- **Spectrum:** a peak marker and readout (the tuner's centre spike is skipped), and tap-to-tune on the trace or waterfall (nearest kHz).
- **Band plan:** the Events detail and the scrub view name the typical allocation at the event frequency ("FM broadcast (typical)"). Allocations differ by country; a frequency with no listed allocation says so.
- **Session card:** SESSION on the Overview lists elapsed and measured time, events by kind, findings per hour (after 10 minutes), loudest and quietest RF level, longest quiet stretch, channels tuned, sweeps and clips. A `session_summary` record is written to the journal when the screen is left.
- **Chime:** off at every session start; when switched on (Events tab) a short two-note ping plays for a logged RF or audio event, at most one every 3 seconds, only with the speaker unmuted and nothing else using it. It uses the replay speaker channel, so the microphone is paused and restarted as after a replay.
- New journal record types `control_run` and `session_summary`; existing records are unchanged.
- New pure modules with host tests: `dead_air_bandplan`, `dead_air_control`, `dead_air_fx`, `dead_air_stats`.

### Added (first batch)

- **Overview RF strip:** the last minute of the measured RF level (one sample per 500 ms), with the baseline (green dashes) and the event limit (yellow dashes) drawn once a baseline exists, and a coloured tick for each RF, audio or mark event.
  A moment with no settled, valid level (retune, sweep, lost receiver) is a gap and the line breaks there; nothing is interpolated. With no data it says "NO LIVE RF LEVEL" or "WAITING FOR A SETTLED LEVEL", and "BASELINE WARMING" before the lines can be drawn.
- **Events tab:** tap a row for that event's time, level, baseline, rise and limit (or the sweep or mark details), and its clip. VIEW CLIP opens the covering clip on Review. NEWER / OLDER page through all 48 events kept in memory; a page you scrolled back to keeps its place as new events arrive.
  The list shows 5 rows per page (was 7) to make room for the detail box.
- Clips now record which events they cover (`ClipInfo::event_seqs`), which is what lets an event find its clip.

### Fixed

- **"No memory" on Play Selected and Start mic:** the LoRa receiver keeps 15.8 MB of PSRAM after it has been opened, which left Dead Air's 1.15 MB audio buffers nowhere to go. A failed PSRAM request now asks every registered holder to give back what it is not using and tries again (`memory_reclaim::alloc_psram`, shared by any screen); the receiver clip ring is retried every 3 s if `open()` could not get it; the serial log gets a `dead_air` warning for every request that needed a reclaim or was refused, with bytes released and what is left. When Dead Air is left, its SD worker frees the clip rings, replay buffer and chime (about 3.5 MB) as its last act, and `open()` waits for that before allocating again. The replay buffer is now the mic ring's buffer (they are never live together): Play Selected stops a running mic, plays, and restarts it, and Start mic is refused while a clip plays. That saves 1.15 MB of PSRAM. Not verified on hardware: whether the LoRa holder agrees to release while the receiver is still on the LoRa band.
- **Keyboard probe noise (shared Tab5 keyboard driver):** with no keyboard attached the driver probed address 0x6D every 2 s and logged an I2C error each time. It now backs off (2 s, 4 s ... up to once a minute, and quickly again after a keyboard that was attached is lost), mutes the ESP-IDF `i2c.master` log tag for the duration of the probe, and logs one line the first time none is found.
- Sensitivity names agree everywhere: QUIET / STANDARD / SENSITIVE (the Events buttons said QUIET / NORMAL / HIGH).
- Removed stale text: "Clip replay: not implemented" on the Audio tab, and "Single channel, not a sweep" beside the sweep controls.

### Tests

- Host suites for the new features: `dead_air_bandplan_tests`, `dead_air_control_tests` (counts per sensitivity, gaps, early stop), `dead_air_fx_tests`, `dead_air_stats_tests`; the renderer suite covers the orb (bounds, throttling, header-only on other tabs), the session, control and scrub overlays (including that nothing under an overlay answers a tap), the peak readout, tap-to-tune and the band map.
- Not covered by any test: the SD worker writing the new journal records, the chime reaching the speaker, trace collection on a live receiver, and how the orb and header beacon look and perform on the panel. These need the Tab5.
- `dead_air_render_tests`: strip with gaps, ticks, dashes, warming and empty states, repeated samples without a whole-screen clear; Events selection, paging, page stability under new events, and VIEW CLIP. `dead_air_format_tests`: strip scale and row mapping.
- The host display stand-in has no pixel buffer, so every refresh takes the whole-body path there. Partial-push behaviour of the new content is not covered by a host test and needs the panel.

## 2026-10-22 - Dead Air dashboard merged into main (PR #203, merge commit `75353cec`)

Dead Air is a logbook of what the receiver measured. It does not detect ghosts, spirits or entities, and it shows no data it did not measure; interpretation is labelled separately from measurement.
The work was developed from 2026-10-18 and landed as one pull request (35 commits). This entry was written afterwards from the commit history and the PR description; the merge itself carried no changelog entry.

### Added

- **Dashboard** (`dead_air_dashboard`, `dead_air_model`, `dead_air_format`, `dead_air_palette`, `dead_air_info`): a native M5GFX dashboard with six tabs (Overview, Spectrum, Audio, Environment, Events, Review), opened from the normal dashboard list and `RTL_UI OPEN DEAD_AIR`.
  The panel is drawn off-screen and only changed tiles are pushed, so it does not flicker; an (i) button on every tab opens a plain-language page for that tab.
- **Receiver data and detector:** live receiver level, baseline and threshold, with an automatic RF-rise event; the detector is suspended while a sweep runs, because every hop is a new channel with no stable baseline.
  Readings from a disconnected or unsettled receiver are invalidated rather than shown. `RTL_DEAD_AIR STATUS` (no authentication, read-only) reports the state; `RTL_DEAD_AIR FLOOD <n>` (authenticated) is a bench command that logs manual marks.
- **Receiver controls:** only real demodulators are offered (NFM, WFM, AM, USB, LSB), each with its standard filter width; a generated sweep profile records its gaps. Leaving Dead Air returns Home to its previous frequency, band label and demodulator.
- **Spectrum and Audio tabs:** a live spectrum trace and a measured-spectrum waterfall; a demodulated-audio level meter and detector.
- **Evidence journal on the SD card:** a bounded event queue and a versioned JSONL session journal at `/orcsdr/dead_air/session_<id>.jsonl`, written by a low-priority worker (core 0). An event counts as saved only after the flush succeeds; a power cut leaves the file ending on a complete line.
  If the card is pulled the recorder reports "failed" and does not silently resume.
- **Clips:** event-linked clips of the receiver's demodulated audio (5 s before and 5 s after, WAV with a SHA-256 in `/orcsdr/dead_air/clips/`), a clip selector and replay on the Review tab on its own speaker channel at the receiver's volume, and a VERIFY button that checks the file on the card against the recorded hash.
  Microphone clips are opt-in (off by default, explicit start, a visible indicator, separate files, the speaker state recorded).
- **Host tests and CI:** ten host test suites (model, journal, renderer and touch, SD sink, sweep, palette, audio, clip planner, formatting, popup text) and the `Dead Air host tests` workflow.

### Fixed in the same change

- Mic clips recorded after a replay were constant-value silence (the shared I2S input stuck on one value); the input is restarted and such clips are refused.
- **General fix inside this change (required):** `0608e3b1` moves cold FT8 buffers (`Runtime g`, the decode chunk, two waterfall lines) to PSRAM with `EXT_RAM_BSS_ATTR`, freeing 23.9 KB of internal RAM. Dead Air's own statics would otherwise tip the boot-time 40 KB DMA pool over and abort start-up.
  A host stub, `tests/ft8_ui_stubs/esp_attr.h`, keeps `ft8_dashboard_tests` building.

### Validated on hardware (Tab5 on COM39, 2026-10-22)

- Boot with no `Could not reserve internal/DMA pool`; all six tabs; the (i) popups; MARK EVENT writing an event and a receiver clip; the journal fetched from the card (event, clip, clip_summary and session_end records); a clip fetched from the card hashing to the recorded SHA-256 and VERIFY agreeing;
  mic clips, replay and VERIFY on the fixed build; antenna unplug and replug (the level moved, but the replug rise stayed below the Standard threshold, so no event fired).

### Known limits and risks

- A real RF-rise event has not been captured on the final build; ENV III hardware (temperature, humidity, pressure) had not arrived.
- The SD worker shares the card with other tasks (UI loop, SD_GET, ACARS archive, FT8 SHOT), and `orcsdr_storage` has no lock and no unmount or remount: a pulled card needs a reboot before a new session can write.
- After leaving Dead Air for Home the band label first came back as BROWSE instead of SHORTWAVE (issue #206, fixed by `f3798608` before the merge).
