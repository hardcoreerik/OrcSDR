# E4000 gain controls and connected receiver name

Source candidate; firmware/RF acceptance is still pending. The driver gain
implementation is local commit `3d75e57` in the separate esp-rtl-sdr repository.
It uses the existing manual-gain and tuner-AUTO APIs and returns 14 nominal
steps from -1 to 42 dB. E4000 remains experimental. RTL digital AGC is separate
and is not advertised for E4000 by this change.

Home already builds its gain slider from the driver's returned ladder. Once
the new driver is pinned in a firmware candidate, it can enable those existing
controls. Authenticated serial uses the same driver API:

```text
RTL_DRIVER GAINMODE AUTO
RTL_DRIVER GAINMODE MANUAL
RTL_DRIVER GAIN -10
RTL_DRIVER GAIN 190
RTL_DRIVER STATUS
RTL_GAIN STATUS
```

Raw gain values are tenths of dB; the driver selects the nearest supported
step. Serial validation and IQ test tooling now permit the -1 dB first entry,
and status parsing accepts signed values. A successful queued request is not
by itself proof of a completed USB write; check settled mode/gain and errors.
Home's unavailable message no longer incorrectly asserts a frequency limit
for every unsupported tuner/mode.

Home's header previously hardcoded `RTL-SDR`, independent of the receiver
label already used in its footer. Both now show the connected model. USB
identity `Nooelec` with an XTR product string is cached at enumeration/ready;
the E4000 profile then displays **Nooelec XTR**. An unidentified E4000 retains
the generic E4000 label. This does not change capability decisions. Nooelec
SMArt V5 displays Nooelec V5 and is no longer incorrectly marked experimental
in the app; E4000 retains its provisional status.

The 11-character XTR name fits the existing 12-byte receiver field. The
bounded 90-pixel header slot uses the small font for long names. Receiver
changes repaint the existing header widget; no layout, framebuffer, PSRAM or
DMA allocation was added. The identity cache adds one atomic boolean and avoids
per-frame driver queries that could produce a changing label on lock timeout.

Focused checks:

```powershell
python tools/test_home_receiver_header.py apps/orcsdr-tab5/build-offset/home-header-test
./apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -SelfCheck
```

The compiled test extracts the actual identity/label and header-drawing
functions with a recording stand-in. It checks identified XTR and generic
E4000 labels, model changes, bounded text and zero full-screen clears. It
does not exercise the entire Home renderer or establish physical flicker
acceptance. The earlier baseline failed the XTR-label assertion; the changed
header test and signed-gain parser self-check pass. The header check is added
to the existing cadence/history workflow, but GitHub CI has not run this local
candidate. Display appearance and the live gain controls need COM39 validation.

COM39 currently runs the earlier isolated HAL-fix image; these source changes
have not been flashed. A gain/identity firmware build must retain that local
HAL correction and the approved display dependency patch, verify driver pin,
source/image hashes, and record its reboot explicitly before physical tests.
