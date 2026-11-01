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

The existing PowerShell gain sweep targets V3 and can reset this tablet when
opening the serial port. The new E4000 smoke runner instead uses the Python
serial client on one persistent connection:

```powershell
python tools/test_e4000_gain_serial.py --self-check
python tools/test_e4000_gain_serial.py --run --port COM39 --key KEYFILE --output NEW-results.json
```

The self-check passed without device access. The hardware run has not been
executed. It requires an already-streaming E4000 advertising both gain
capabilities, checks the returned 14-step ladder, requests each step and AUTO,
waits for settled status plus advancing IQ byte count, checks uptime and loss
counters, and restores the initial gain/controller. It saves only selected
protocol responses, never pairing/authentication data, and refuses to overwrite
an existing result. A recently restarted device fails preflight. The test does
not tune, start, flash or operate a signal generator; port-open behavior still
needs physical uptime validation. Serial success is not a calibrated RF gain
measurement; the cabled tone sweep remains an independent acceptance gate.
