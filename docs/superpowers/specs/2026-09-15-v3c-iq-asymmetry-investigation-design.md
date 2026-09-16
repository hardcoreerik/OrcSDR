# V3c IQ Asymmetry Investigation Design

## Goal

Find the first RTL-SDR Blog V3c state transition that changes a two-sided complex-IQ spectrum into the reported one-sided state. Do not change receiver behavior until raw captures and driver-state evidence identify a root cause.

## Boundaries

- Test OrcSDR commit `fd9d0427cfd011d8444b014cc21188906d88452a` with `esp-rtl-sdr` commit `e1ca40e04f8140245d56837cd149bf901f771441` (`0.8.0-rc2`).
- Keep the existing OrcSDR and driver checkouts untouched by using isolated branches/worktrees.
- Capture raw interleaved CU8 immediately after driver delivery and before FFT or demodulation.
- Do not change FFT, waterfall, filtering, sample interpretation, or UI behavior.
- Do not copy undocumented third-party register sequences. Diagnostic reads, if needed, must use already-supported first-party control-transfer mechanics.
- Keep source, build, flash, serial state, IQ analysis, audio, RDS, and physical spectrum observations as separate claims.

## Capture path

Extend OrcSDR's existing bounded PSRAM IQ recorder with a `diagnostic` kind limited to one second. The shared DSP task appends the original driver-delivered CU8 bytes before spectrum or audio processing. Existing authenticated `RTL_IQ_GET_*` binary retrieval remains the transport.

An authenticated host command starts the capture with a short transition label. The host runner tunes the requested route, waits for streaming, retrieves the bytes, verifies their SHA-256, and writes adjacent JSON metadata containing device/profile, requested and reported RF, rate, route, gain, counters, transition label, capture identifiers, and the latest driver RF-state line.

## Analyzer

`apps/orcsdr-tab5/tools/analyze_rtl_iq.py` reads CU8 as independent I and Q channels, removes each DC mean, applies Hann-windowed complex FFT frames, and averages linear power. It writes spectrum and waterfall PNGs, a text report, and optional CSV.

The report includes half-spectrum means and medians outside configurable DC and Nyquist guards, half-power delta, I/Q means and RMS, RMS delta, correlation, clipping percentage, and an ASCII spectrum. Classification remains descriptive until empirical V3c GOOD, V3c BAD, and V4 baseline measurements exist.

## Driver-state evidence

The isolated driver branch emits one concise `V3C_RF_STATE` line after successful cold tuning and hot retuning. It reports only state the driver actually owns: profile, RF, sample rate, normal/direct route, matched PLL/demod IF, direct NCO when applicable, gain/AGC state, and transition source.

If GOOD and BAD captures have identical intended state, add targeted read-only register verification in a second instrumentation step. No register write changes are permitted during investigation.

## Reproduction order

Run V3c captures at 99.1 MHz after: cold boot; 99.1 -> 96.1 -> 99.1; another normal VHF round trip; 99.1 -> 10 MHz -> 99.1; 99.1 -> 147.3 kHz -> 99.1; radio close/reopen; and USB detach/reattach. Run a Blog V4 99.1 MHz control with the same firmware and antenna where practical.

Stop at the first reproducible GOOD-to-BAD transition long enough to compare raw IQ and driver state. Present the comparison table, strongest evidence-backed hypothesis, proposed driver-only correction, and required regression tests before implementing any correction.
