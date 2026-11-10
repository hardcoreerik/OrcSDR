# Driver 0.9.4 integration

Pins public esp-rtl-sdr release v0.9.4, commit43474869e061a78c209091f9b3a65c498ae64fb4.
No local checkout dependency, SDK HAL override, startup tracing or diagnostic
firmware image is included. Existing repository display/Hosted patch process
is used. Other in-progress worktrees and branch heads are not modified.

## Current maintainer operational clarification (2026-11-04)

The Nooelec XTR/E4000 driver works within OrcSDR for frequencies within its declared tuning range. A dashboard for a frequency E4000 cannot tune (e.g., broadcast AM without an appropriate external converter) can still open, but intentionally does **not** issue an invalid tuner request. This is functioning frequency-capability handling, **not** an unintegrated or failed driver. Consistent disabled-state UX across all dashboards and full RF-characterization remain separate work. See [field acceptance](MAINTAINER_FIELD_ACCEPTANCE_2026-11-04.md).

## Firmware behavior

- Apply hardware offset tuning only when the driver advertises support.
  Driver IQ already has the requested center; no additional app mixer.
  DefaultON, volatile preference; not a new persistence promise.
- Authenticated `RTL_DRIVER OFFSET ON|OFF` shares driver validation;
  `RTL_DRIVER STATUS` additionally emits `RTL_OFFSET_STATUS` with actual offset,
  tunerLO and IQcenter. Unsupported receivers report unsupported without
  changing their tune path.
- Support signed manual gain requests down to-10 tenths dB. Driver chooses
  nearest supported step; existing common gain handlers remain in use.
- Cache model identity at enumeration/ready and clear on disconnect.
  Display Nooelec XTR for matching descriptors, otherwise E4000. NooelecV5
  and E4000 are supported labels, not provisional UI warnings.
- Raw diagnostic IQ recorder accepts custom-rate blocks; decoder-specific
  recording rules remain unchanged.

Home model updates redraw the existing header region. No new framebuffer,
DMA or PSRAM allocation; one atomic identity flag and one offset preference.
Host recording-display check verifies bounded label/model updates with zero
full-screen clears. Physical flicker/reception acceptance is a separate gate.

## Branch coordination

DroneWatch PR213 also edits ui/main.cpp; coordinate/recheck against latest
main immediately before merge. WEFAX218 and FT8maps204 have no direct file
overlap in this scoped change. Do not merge/rebase/delete those branches.
This integration is based on main0b60a166. Source labhistory stays on
codex/e4000-offset-20261031; no bulk evidence is duplicated here.

## Validation

Focused commands:
`python tools/test_iq_diagnostic_rate_gate.py`
`python tools/test_home_receiver_header.py apps/orcsdr-tab5/build-integration/host-header`
`python tools/test_e4000_gain_serial.py --self-check`
`python tools/tab5_exact_response.py`
All passed locally. CI includes these offline gates; serial self-check does
not exercise hardware. Compile results are recorded in the PR separately.

The original lab used pre-release driver revisions and a separate HAL override;
its RF results are useful history, not physical proof of this clean tree.
No flash is authorized by PR preparation. Wider serial timeout, AUTO RF and
cross-device reception checks remain open. The driver v0.9.4 source retains
legacy experimental diagnostics naming; app support classification follows
maintainer direction without claiming calibrated all-band gain.

Waveshare/Hosyond full radio UI is not present on current main. Its separate
standalone display target is not a substitute for an SDR integration build;
that board branch must consume this PR and validate its own configuration.

## Clean build result

Native ESP-IDF 5.5.4 Tab5 compile/link and post-link gates passed using the
repository M5GFX/Hosted patches, without an embedded C6 image or HAL override.
The initial stale-lock attempt was stopped before compiling; dependency
resolution was explicitly updated. Only driver-related lock entries changed.
Fetched driver source matches the released revision after CRLF/LF normalization.
See validation/driver-094-integration/build.json for image/source hashes.
CI documentation, focused tests and storage checks passed on code-equivalent
head 573ea007. No Waveshare full-radio compile, flash or new physical acceptance
is claimed. Board/hardware validation remains a documented follow-up; the maintainer approved merge after review fixes and software checks, without a flash.

## Review correction validation

Quoted immutable pins, E4000 self-check capabilities, direct-sampling gain
help, command usage and capture-rate integrity were corrected. The diagnostic
routing check compiles the production condition for matching default/custom
rates and rejects mismatches; it also detects an enclosing custom-rate guard.
Home recording-display tests cover the three gain-unavailability messages.
PowerShell self-check and documentation truth checks pass. The updated native
build passed compile/link/post-link checks; its image is recorded in build.json.
No new hardware acceptance is implied.
