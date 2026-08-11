# Implementation phasing: Grok review gaps

Implementation plan for the gaps tracked in `Roadmap.md`. Follows the same
phase/exit-criteria shape as `PROJECT_STATUS.md`'s P0/P1/P2 roadmap. Each
phase is scoped to land independently and keep the firmware building and
flashable at every step — no phase should require a multi-day broken build.

## Ground rules

- No behavior changes bundled into a structural move. A file split that also
  changes DSP math or touch geometry is two PRs pretending to be one.
- Every extracted module keeps `main.cpp`'s existing global state working
  through the transition (shared externs/headers), rather than trying to
  redesign ownership boundaries in the same pass. Cleaning up shared mutable
  state is a explicitly *separate*, later effort — bite off structure first.
- Flash-and-verify on hardware after each phase closes, same as every other
  change in this repo (`PROJECT_STATUS.md`'s evidence-label discipline
  applies to refactors too — "Build-verified" is not "Hardware-verified").

## Phase 1 — split `main.cpp` into modules

Goal: break the ~266 KB single translation unit into logical modules without
changing behavior, closing Roadmap Gap 1 and setting up Gap 2 (legacy USB
deletion becomes a one-file diff) and Gap 5 (touch/draw abstraction has
somewhere to live).

Target module boundaries (mirrors the review's suggested split):

- `dsp/` — demodulators (FM/AM/SSB/NFM), the stereo pilot/L-R decoder, AGC
  and soft-limiter (`shape_audio_sample`, `fm_soft_limit`), filter state
  (`RtlAudioState`), spectrum FFT (`draw_spectrum`'s compute half).
- `ui/` — spectrum/waterfall drawing, per-band dashboards (CB, LoRa, FM),
  shared controls (`draw_sdr_controls`, `draw_sdr_header`), touch routing.
- `radio/` — band/session state (`RtlBand`, `rtl_ui_band`,
  `queue_local_rtl_listen`), capture state machine, retune logic, preset
  scan state machine.
- `host/` — serial protocol (`RTL_REC_START` and friends), SD put/get
  transfer, journal/auth (`load_state`, `append_journal`, `hmac_matches`).
- Driver boundary unchanged — `components/rtl_sdr_v4_esp` stays the only USB
  path once Phase 1 lands (actual legacy-path deletion still gates on the P1
  soak test in `PROJECT_STATUS.md`, not on this split).

Sequencing (each step keeps the build green):

1. [ ] Extract `dsp/` first — it has the fewest cross-cutting dependencies on
      UI globals. Move `RtlAudioState`, `demodulate_fm`, `shape_audio_sample`,
      `fm_soft_limit`, stereo-decoder constants, into `dsp/fm_demod.{h,cpp}`.
2. [ ] Extract `host/` next — serial command parsing and SD transfer are
      already fairly self-contained (`copy_to_tab5_sd.ps1`'s counterpart
      protocol handlers). Move into `host/serial_protocol.{h,cpp}`.
3. [ ] Extract `radio/` — band/session/capture state machine, into
      `radio/session.{h,cpp}`.
4. [ ] Extract `ui/` last, since it currently touches the most shared state
      (spectrum globals, per-band dashboard functions, touch handlers). Split
      further by band if the file is still large after the first pass
      (`ui/dashboard_cb.cpp`, `ui/dashboard_lora.cpp`, `ui/dashboard_fm.cpp`,
      `ui/spectrum.cpp`, `ui/controls.cpp`).
5. [ ] `main.cpp` left behind: `setup()`/`loop()`, task creation, and
      wiring — should shrink to a few hundred lines.

Exit: `main.cpp` under ~500 lines, each extracted module compiles and links
with no behavior change, full flash-and-smoke-test on hardware (tune FM,
tune CB, tune LoRa, confirm dashboards and touch still respond identically).

## Phase 2 — minimal CI

Goal: catch a header/driver-example break automatically, closing Gap 3.

1. [ ] Add `.github/workflows/build.yml`: on every push and PR, run
      `pio run` for the Tab5 UI environment (`m5tab5_ui`) and the LoRa test
      environment (`m5tab5_lora_test`).
2. [ ] Add a second CI job that builds `examples/p4_serial_smoke` standalone
      against `components/rtl_sdr_v4_esp`, independent of the Tab5 app —
      this is the review's specific concern (nothing currently proves the
      published API contract still compiles against its own example).
3. [ ] No hardware-in-the-loop testing in CI at this stage — that's a much
      larger investment (self-hosted runner with an attached Tab5 + Blog V4)
      and isn't blocking the immediate gap. Track it as a future item only if
      the project reaches the point of wanting hardware regression coverage.

Exit: a broken build or a broken example fails a GitHub Actions check before
merge, instead of being caught by a human running `pio run` manually.

## Phase 3 — tool-shell abstraction

Goal: give new tools (RDS panel, Analyzer, Gain Lab, IQ dump) a shared shape
to plug into instead of growing `handle_*_touch`/`draw_*_dashboard` pairs ad
hoc, closing Gap 5. Sequenced after Phase 1 because it needs the `ui/`
module boundary to exist first — retrofitting an abstraction into the
monolith and then re-splitting it is wasted motion.

1. [ ] Define a minimal `Tool` interface: `draw(bool static_panel)`,
      `handle_touch(x, y) -> bool`, `on_band_enter()`, `on_band_exit()` —
      shaped after the existing `draw_fm_dashboard`/`handle_fm_touch` pair,
      since that's the newest and most self-contained example already in the
      codebase (background JPEG + live overlay + explicit touch region
      checks, no shared mutable draw state beyond the RAM-cached VU sprites).
2. [ ] Migrate the FM dashboard to implement `Tool` first, since it's the
      newest code and the best-isolated test case for the interface shape.
3. [ ] Migrate CB and LoRa dashboards once the interface has proven itself
      against FM's more complex case (presets, scan state machine, live
      spectrum embed).
4. [ ] Land the in-flight RDS panel as a `Tool` implementation directly,
      rather than a fourth hand-rolled `handle_*_touch`/`draw_*_dashboard`
      pair — this is the concrete payoff the review is pointing at.

Exit: adding a new radio-mode tool means implementing one interface and
registering it, not touching the shared touch-dispatch chain
(`handle_sdr_touch`'s `if (handle_cb_touch(...)) return; ...` chain) by hand.

## Phase 4 — RDS decoder (separate initiative, not a Grok-review gap)

Goal: decode FM RDS (station name, PTY, RadioText, PI) entirely on-device —
no offload, no host PC, matching the project's standalone-appliance design.
Staged deliberately: each stage is independently verifiable against a real
broadcast before the next stage is written, because bit-level symbol/timing
recovery is the kind of DSP that is either exactly right or produces zero
lock with no useful failure signal — and this project has no RF-in-the-loop
test rig, so "compiles and reasons correctly" is the ceiling of confidence
until a stage is checked against real air.

Architecture (confirmed independently by two review passes — Codex's
proposed design and this project's own working pilot/sub-carrier code agree
on the tap point): RDS lives at 57 kHz, exactly 3x the 19 kHz stereo pilot,
inside the full FM multiplex (MPX). It must be tapped **before** the ~16 kHz
mono audio low-pass filter in `demodulate_fm` — that filter would otherwise
discard it entirely. This project's stereo decoder already taps the
composite discriminator output (`phase`) at exactly that point for the
pilot and 38 kHz sub-carrier resonators; RDS Stage 1 (landed) reuses the
same tap and the same two-pole resonator technique already proven correct
for pilot/sub, just re-tuned to 57 kHz.

Sample-rate note: Codex's proposal suggests resampling the MPX to a rate
with clean integer ratios to RDS's clock rates (171 kHz, matching redsea's
internal rate, or 228 kHz where pilot/RDS/bit-rate all divide evenly). This
project instead keeps RDS at the existing 240 kHz post-RF-decimation rate
(`kFmRfDecim`-derived), using continuous-coefficient IIR resonators the same
way the pilot/sub decoders already do — no integer sample/cycle relationship
is required for an IIR resonator (coefficients are `r = 1 - pi*BW/fs`,
`2r*cos(2*pi*f0/fs)`, valid for any fs/f0 pair). This avoids adding a
resampling stage and avoids touching the RF decimation ratio that the
already-hardware-verified mono/stereo audio path depends on. Revisit only if
Stage 2's symbol timing recovery proves easier at a resampled rate in
practice.

Component boundary (per Codex's separation point, which matches this
project's own driver-boundary discipline in `PROJECT_STATUS.md`): the
`rtl_sdr_v4_esp` driver knows nothing about RDS. RDS consumes only the
already-demodulated MPX/`phase` stream, same as the stereo decoder. Code
lives in `main.cpp` for now (matching how the stereo decoder landed) but is
written as clearly-bounded functions/state so it lifts mechanically into
`components/rds_decoder/` during Phase 1's module split, rather than being
entangled with `demodulate_fm`'s internals beyond the one tap point.

1. [x] **Stage 1 — carrier detect.** 57 kHz two-pole resonator on the
      composite tap, rectified-envelope lock-style presence detector
      (mirrors the pilot decoder's `pilot_env`/`kStereoLockOn`/`Off`
      pattern exactly). Published as `rtl_rds_signal_dbfs` (a monotonic
      log-scale indicator, not calibrated true dBFS — same caveat as any
      other unitless meter reading in this codebase) and
      `rtl_rds_carrier_present`. Wired to the FM dashboard's STATION/RDS
      card as a `SEARCHING...` / `RDS CARRIER` badge, replacing the static
      "NEEDS DSP WORK" placeholder. **Hardware-verified 2026-08-10** — badge
      went green on 96.1 KZEK (Tab5 + Blog V4). Confirms the 57 kHz tap and
      resonator are seeing real RDS energy, not noise, before any bit-level
      DSP was built on top of it.
2. [x] **Stage 2 — physical layer: bits and blocks.** **Implemented and
      hardware-accepted** — do not parse fields out of blocks that were never
      confirmed to sync on real air:
      - Carrier-independent complex demodulation: raw MPX is mixed to 57 kHz
        baseband with a recursive complex NCO and a ~2.4 kHz one-pole LPF.
        Complex differential chip-pair dot products cancel unknown carrier
        phase and slow tuner offset without the prior Costas loop, whose
        direction/clamp behavior did not survive captured-MPX replay.
      - Symbol timing at 2375 chips/sec (2 chips per RDS data bit) uses four
        evenly spaced fractional-chip tracks and both chip-pair polarities.
        The verified capture measured a small `-10 ppm` sample-clock
        calibration; it remains an explicit hardware tuning constant.
      - Differential Manchester (biphase) bit recovery compares consecutive
        complex pair vectors. Absolute carrier polarity cancels in the dot
        product, while the two pair alignments cover the half-bit ambiguity.
      - Block sync via the offset-word/CRC search verified in this planning
        pass (generator polynomial `x^10+x^8+x^7+x^5+x^4+x^3+1`, confirmed
        by direct polynomial-division check against all five offset words
        A/B/C/C'/D before any C++ was written against it). Continuous
        sliding-window syndrome search per hypothesis; declares lock once 4
        consecutive matches land at the expected 26-bit spacing in
        A,B,C(or C'),D order, then switches to cheaper positional checking
        (only testing the syndrome at the next expected boundary, not every
        bit) and drops back to search mode after 32 consecutive bad blocks.
        When every established timing hypothesis loses lock, the RDS-only
        demod state resets once; the captured outage proved that a clean state
        reacquires data that stale accumulated state could not.
      - Block error rate (BLER) exposed to both the UI (FM dashboard's
        STATION/RDS card) and a throttled serial diagnostic line
        (`RDS_STAGE2 locked=... bler=...% good=... total=... A=... B=...
        C=... D=...`, every 2s) — Codex's review correctly points out BLER
        is a genuinely useful RF quality metric (multipath damages RDS
        specifically because the group structure is deliberately per-block
        CRC-protected), not just a decoder-internal detail.
      - **Hardware acceptance 2026-08-11:** deterministic replay locks with
        PI `0x5277`; a three-minute live run stayed locked at every 5-second
        sample from 30s through 150s while clean-state reacquisition reset the
        counters several times. One later outage lasted between 10 and 15
        seconds and recovered automatically by 165s. USB overruns, driver
        drops, and audio drops remained zero throughout. This accepts Stage 2
        block recovery while leaving BLER improvement and Stage 3 metadata as
        separate work.
      - **Autonomous test loop unblocked**: fixed two boot-time gates that
        were silently preventing hands-off iteration — the splash screen's
        indefinite tap-wait (now skipped, see `kSkipSplashGate` in
        `setup()`) and the home screen's tap-to-start gate (now an
        auto-start once `rtl_device_ready()` first goes true, restoring
        the last band/frequency instead of hardcoding FM). Also found that
        the entire demod chain — mono, stereo, *and* RDS — only runs when
        `rtl_audio_enabled` is true (audio and DSP are coupled, not
        separable in the current code), so auto-start also enables audio.
        Net effect: flash → reboot → active reception with real telemetry,
        zero touch input, in about 15 seconds. This is what made the (2)
        and (3) fixes above possible without asking for a manual re-tune
        each time.
3. [ ] **Stage 3 — station metadata.** Once blocks sync reliably: PI
      (block A, every group), PTY + TP (block B), PS name (group type
      0A/0B, 2 chars per group from block D at the segment address in block
      B bits 0-1), RadioText (group type 2A/2B, 4 or 2 chars per group from
      blocks C/D at the segment address in block B bits 0-3). Publish via a
      mutex-protected buffer (mirrors the existing `lora_message_mux`
      pattern already used for cross-thread string data in this codebase),
      not raw atomics, since these are multi-byte strings.
4. [ ] **Stage 4 — advanced (deferred until 1-3 are hardware-proven).** AF
      (alternate frequencies), RT+ (structured artist/title), CT
      (clock/date), EON, ODA, RDS2 detection. Not scoped further until the
      core path is verified — matches `PROJECT_STATUS.md`'s own "do not
      claim ahead of evidence" discipline.

### Aside: recurring audio pause investigation (2026-08-10)

Unrelated to RDS, but found via the same autonomous serial-driven test loop
this phase's work unblocked. Reported symptom: an audible pause roughly
every 5 seconds. Confirmed independently via a physical recording, not just
one observer.

- **Ruled out**: RDS Stage 2 processing. Gated the entire Costas/Gardner/
  block-sync block behind `constexpr bool kRdsStage2Enabled = false`
  (compiler dead-code-eliminates it entirely at zero runtime cost) and
  flashed — pause was still there, unchanged. Not the cause.
- **Attempted and reverted**: moving `rtl_app` (the task started in
  `initialize_rtl_sdr_host()`, nominally "touch/retune/spectrum/dashboard
  UI") from core1 to core0, to relieve core1 for the RTL-SDR driver's
  inline demod+speaker delivery task (`rtl_iq_del`, core1, prio 18) without
  touching that delivery path itself — a prior attempt to split *that*
  path into a cross-task queue is on record in `on_rtl_driver_event`'s own
  comment as having broken both audio and graphics, so this was
  deliberately a different, narrower experiment. **Broke capture
  entirely** — signal stuck at floor, screen stuck on the home/"Host
  offline" state, `rtl_app` never actually started the stream. Root cause:
  `rtl_app` is not pure UI, it's on the critical path that consumes
  `rtl_capture_requested` and issues the actual driver start/retune calls.
  At its low priority (5), placed on core0 behind the USB host (prio 20)
  and client (prio 19) tasks, it was starved outright rather than merely
  slowed. Reverted to core1; confirmed reception resumed normally
  (`RTL_SIGNAL` back to real dBFS values, stereo re-locked).
- **Real cause still unknown — and behaves like a marginal/borderline
  timing issue, not a clean deterministic bug.** No code has been found
  with an actual ~5-second period despite searching (checked
  `kSessionTimeoutMs`-adjacent logic, LoRa SD-log flush intervals, WiFi
  scan/retry — none match and/or aren't on the active FM code path). The
  pause was reported gone, then back, then gone, then back again within
  moments, across builds that didn't always change anything relevant —
  including one flip with *zero* firmware change in between. That pattern
  (same build, presence inconsistent even second-to-second) rules out a
  clean single-cause bug and points to something right on the edge of a
  timing budget: occasionally missing it, occasionally not.
- **Isolated to the FM dashboard specifically** — switching to BROWSE
  band (generic shared spectrum/waterfall UI, no custom dashboard) removes
  the pause entirely on the same hardware, same station, same session.
  This is the strongest lead: whatever's marginal is in
  `draw_fm_dashboard`'s redraw path, not the demod/audio pipeline itself.
- **IQ delivery timing measured directly and came back clean.** Added a
  diagnostic (`RTL_IQ_GAP`, in `on_rtl_driver_event`'s `EVT_IQ_BLOCK`
  case) logging the gap between consecutive IQ block deliveries — over a
  25s window on the FM dashboard, one 50ms gap at boot (startup
  transient) and nothing recurring. This rules out "dashboard redraw
  stalls the demod/IQ-delivery task" as the mechanism (that task's timing
  is fine); the contention is more likely between display SPI traffic and
  the speaker's own DMA/output path — two peripherals that may share
  lower-level bus resources even though they're logically independent
  tasks. Consistent with `PROJECT_STATUS.md`'s existing, pre-dating-this-
  session "live speaker vs. recorded PCM" open performance gate (clean
  recorded WAVs, live speaker issues, isolated to "speaker queue/DMA/
  output after the DSP tap") — plausibly the same underlying gate, newly
  visible because the FM dashboard draws meaningfully more per redraw
  cycle than the UI that existed when that gate was last measured.
- **Most promising untried fix**: `draw_fm_dashboard`'s periodic (~200ms)
  redraw currently repaints almost everything unconditionally every call
  — VU needles (two full sprite blits), frequency/SIG/VOL text, presets,
  tuner — regardless of whether the underlying value actually changed.
  `draw_signal_meter` elsewhere in this codebase already does dirty-
  checking (skip the SPI write if the value hasn't moved since last
  frame); the FM dashboard doesn't. Reducing SPI traffic there is a
  concrete, bounded change that should ease whatever margin is being
  exceeded, even without a definitive smoking-gun timestamp to point at
  given how borderline/sporadic the symptom is. Not yet implemented.
- Recorded 12s of post-demod PCM to SD (`RTL_REC_START`/`STOP`, taps
  audio right before `playRaw`, same point this project's own prior
  recorder validation used) for offline waveform inspection — pulling it
  off-device needs the radio stopped first (`SD_GET_ERROR radio_busy`),
  which needs the `authenticated` gate; the device was already paired to
  a different, unknown key from an earlier session, so a fresh PAIR/AUTH
  attempt returned `PAIR_LOCKED` and this wasn't completed.
  `tools/get_from_tab5_sd.ps1` was added this session as the missing
  GET-direction counterpart to the existing PUT script, for when this (or
  a future) capture needs pulling — either with the existing pairing key
  or after a manual STOP tap.
- **Next step, not yet attempted**: if `rtl_app` really does need to stay
  off core0 as a monolith, the natural follow-up is separating its two
  responsibilities — pull the actual capture-state-machine/driver-command
  logic into its own small, high-enough-priority piece that must stay
  wherever the driver needs it, and move only the UI-drawing portion
  (spectrum FFT, dashboard redraws, touch hit-testing) to core0. That's a
  more invasive change than this pass attempted and should be scoped as
  its own task, not bolted onto more trial-and-error core reassignment.
- **Symptom escalated mid-investigation** to something more severe than
  the original "every ~5s" report: sound going fully choppy/hanging after
  roughly 10-15s of continuous playback, recoverable by toggling
  `RTL_SOUND OFF` then `ON` (which resets `rtl_audio_play_count` and
  restarts the speaker), and — per direct user confirmation — happening
  **regardless of band/UI mode**, not just on the FM dashboard. That last
  point rules out the dashboard as the root cause, since browse mode never
  calls `draw_fm_dashboard`. Added `RTL_IQ_GAP` diagnostic (gap between
  consecutive `EVT_IQ_BLOCK` deliveries, logged when >30ms) directly in
  `on_rtl_driver_event` to get hard timing data instead of continuing to
  reason from downstream symptoms.
- **Attempted and reverted**: added dirty-checking to
  `draw_fm_dashboard`'s periodic redraw (skip repainting chrome — freq/
  SIG/VOL/RDS badge — when the underlying values hadn't changed; changed
  the 94-segment INPUT LEVEL bars to only repaint the delta between old
  and new lit-count instead of all 94 every cycle). This was reasoned
  from real evidence (isolated-to-dashboard correlation, IQ-gap clean
  baseline) but had **not yet been isolated from the "regardless of
  mode" report** when it was written — that correction arrived only
  after this change was already flashed. Immediately after flashing it,
  `RTL_IQ_GAP` showed continuous 30-48ms gaps starting ~111s into the
  session and persisting (very different from the earlier clean
  baseline: one 50ms gap in 25s). Given the "regardless of mode" finding
  means dashboard cost can't be the root cause, and given the strong
  temporal correlation between this specific change and materially worse
  measured gaps, **reverted rather than risk leaving unverified code on
  a device that was actively misbehaving** — could not rule out the
  change having introduced a real problem (vs. coincidentally being
  flashed right as a separate, pre-existing degradation kicked in), and
  didn't have time to isolate which before the user needed a working
  device back. `main.cpp` is back to the pre-dirty-check state; `RTL_SOUND`
  and `RTL_IQ_GAP` were kept (additive, no behavior change, needed for
  further diagnosis).
- **Leading theory now**: this matches `PROJECT_STATUS.md`'s own
  pre-existing, undated "live speaker vs. recorded PCM" open performance
  gate (clean recorded WAVs, live speaker issues, isolated to "speaker
  queue/DMA/output after the DSP tap") almost exactly — same symptom
  shape (fine initially, degrades with sustained playback), same
  recovery mechanism implied (a fresh start clears it). Plausible that
  this bug predates this session entirely and is only newly exposed
  because this session's auto-start/auto-sound-on changes are the first
  time the radio has run continuously and unattended long enough to hit
  it reliably — previously, the tap-gated flow made long unattended
  listening sessions unlikely to happen by accident. **Not confirmed.**
  Next real step is the speaker/DMA layer itself (M5Unified's internal
  queue behavior under `dma_buf_count=24, dma_buf_len=512`, ~256ms of
  buffered headroom — consistent with a slow backlog taking 10-15s to
  exhaust it), not more application-level redraw tuning, given the "any
  mode" finding.

Exit for the phase as a whole: FM dashboard shows real `PI` / `PS` /
`PTY` / RadioText from an actual over-the-air broadcast, with BLER
displayed, on the Tab5 hardware — not just a build that compiles.

Reference material (used as algorithmic reference only, not transplanted —
see licensing note): redsea (github.com/windytan/redsea, MIT) is the
clearest concrete reference for the DSP chain shape (57 kHz NCO mix, ~2.4
kHz LPF, symbol timing, differential decode). gr-rds
(github.com/bastibl/gr-rds) is useful for cross-checking the block-sync/CRC
implementation but is GPL-3.0 — use as a validation oracle on a PC, not as
a code source, to keep this project's licensing posture unchanged.

## Phase 5 — RDS capture/replay test fixture (do this before more live RDS iteration)

Goal: stop needing a reflash + a manual re-tune + a live serial capture for
every RDS DSP change. Two iterations in a row against 96.1 KZEK each cost a
full flash-and-hands-on-the-device cycle; a recorded-signal fixture turns
that into a repeatable, zero-hardware test.

Hardware-accepted checkpoint (2026-08-11): the branch captured 8 seconds of
96.1 KZEL MPX (`1,920,000` signed-16 samples at 240 kS/s), saved and replayed
the file on Tab5, and copied all `3,840,000` bytes to the PC with matching
device/host SHA-256
`19e9207691c796b24b60740378878e6020af3a6bc06508a42acbec4fa3c3d856`.
Redsea decoded PI `0x5277`, PS `KZEL`, and continuous 0A/2A groups. The same
fixture gives OrcSDR block lock (`267/364` good blocks, 26.6% BLER). A second
capture taken during an OrcSDR live unlock transferred with matching SHA-256
`9a4fd9df82a7dbf88062835c0668593943662f3847d1043269b02b30ae92a724`;
Redsea decoded 83 valid groups from it, and clean on-device replay locked with
PI `0x5277` (`137/324`, 57.7% BLER). That comparison isolated the live failure
to accumulated decoder state and directly motivated the reset-on-full-lock-loss
reacquisition fix.

1. [x] **Capture.** Record the composite discriminator output (`phase` in
      `demodulate_fm`, the same 240 kS/s pre-mono-LPF tap the pilot/sub/RDS
      resonators already use — capturing here rather than raw IQ preserves
      full generality: any future filter/carrier/timing change can be
      replayed against it without re-deriving the FM discriminator too)
      into a PSRAM ring buffer during live reception, then flush to SD only
      after capture stops — never write SD from the real-time IQ path, per
      this project's own established rule (see the existing
      `g_audio_rec_buf`/`audio_rec_stop_and_export()` pattern for the
      precedent to follow). Store as fixed-point int16 (phase is bounded,
      roughly ±π — scale by a constant, document it in the file header) to
      keep a useful capture length (10-20s) under a few MB. Add
      `RTL_RDS_CAPTURE_START`/`_STOP` serial commands (see
      `docs/API_SERIAL_CLI.md` for the command-doc convention to follow)
      and write to `/orcsdr/rds_debug/<band>_<freq>_mpx.raw` +
      a sibling `.json` with sample rate, frequency, timestamp, sample
      count, and the fixed-point scale factor.
2. [x] **Replay.** Factor the existing per-sample RDS chain (57 kHz
      complex baseband through differential pairing/block-sync — previously inline in
      `demodulate_fm`) into a standalone function taking one `phase` sample
      and updating `rtl_audio`'s RDS fields, callable both from the live
      per-sample loop and from a replay path that reads a captured file
      back sample-by-sample. This is a mechanical extraction, not a
      redesign — the code already operates on one sample at a time, it
      just needs a real function boundary instead of being embedded inline.
      Add an `RTL_RDS_REPLAY <path>` command that runs the captured file
      through this function end-to-end and reports the same `RDS_STATUS`
      output a live session would, without touching the RTL-SDR at all.
3. [x] **PC oracle (optional but high-value).** Copy a capture off the
      device (`copy_to_tab5_sd.ps1`'s `SD_GET` direction — the tooling
      already exists) and feed it to redsea on a PC after converting the
      fixed-point format to whatever redsea expects as input. If redsea
      decodes the same capture correctly and OrcSDR doesn't, that isolates
      the problem to the RDS decoder specifically — antenna, RF reception,
      and FM demod quality are proven sufficient. This is the single
      strongest debugging signal available for this subsystem and is worth
      the format-conversion effort once Phase 5.1/5.2 land.

Exit: an RDS DSP change (filter coefficient, loop gain, decode logic) can
be tested against a fixed recorded signal without touching live RF or
asking anyone to re-tune a physical device.

Exit achieved on the captured KZEL fixture above. Redsea remains a developer
oracle only; it is not a firmware or end-user runtime dependency.

## Explicitly out of scope for this phasing pass

- Gap 2 (dual USB paths) and Gap 4 (performance gates) are already tracked
  in `PROJECT_STATUS.md` P0/P1 with their own exit criteria and are not
  duplicated here — Phase 1 only makes the eventual legacy-path deletion
  cheaper, it does not schedule the deletion itself (that still gates on the
  soak test).
- No redesign of ownership/threading model for shared state
  (`RtlAudioState`, the various `std::atomic` UI-communication globals) —
  Phase 1 preserves the existing cross-module communication pattern as-is.
  Revisiting that is a legitimate future phase but is a materially larger
  and riskier change that shouldn't be bundled with a mechanical file split.
