# rtl-gate: V3c tuner-bandwidth gate analysis and PC-capture tools

Used for the V3c live tuner-bandwidth work (see
`docs/testing/v3c-tuner-bandwidth-transition-2026-09-28.md`). Python needs numpy and matplotlib.

## One-command regression for whichever dongle is plugged in

    pwsh apps/orcsdr-tab5/tools/run-dongle-regression.ps1 -Label v4 -Level Standard
    pwsh apps/orcsdr-tab5/tools/run-dongle-regression.ps1 -Label v4 -Level Full -SoakMinutes 15 -HotplugCycles 5

Stages: preflight (identify dongle, driver version and pinned commit, recover a faulted driver), smoke,
band-switch stress (FM, airband, WX, UHF, 915, 1090, HF direct-Q, AM; the driver must never reach FAULT),
gain sweep, per-band sensitivity table (`band_snr.py`), optional interactive unplug/replug capture
(`serial_capture.py`, survives board resets), and the soak. Levels: Quick, Standard, Full. Output:
`artifacts/dongle-regression/<stamp>-<label>/report.md` and `results.json`. To judge a driver change, run
it before and after and pass the first `results.json` as `-CompareTo`. The device resets during smoke and
soak. The script drives the board only through `run-tab5-ui-regression.ps1`.

**PC reference receiver for ADS-B (`-PcReferenceGain <dB>`).** A dongle in the PC records raw IQ with `rtl_sdr.exe` at
exactly **2.048 MS/s** (the Tab5 decoder's bit timing is built for that rate and fails the CRC at any other, e.g. 2.0 MS/s)
and `pc_adsb_capture.py` decodes it with OrcSDR's own decoder (`adsb_decode_cu8.cpp`, built under WSL into `~/.orcsdr`),
so the PC and the Tab5 differ only in dongle, antenna and site. `--rtl-adsb` falls back to the stock `rtl_adsb`, which allows
five bit errors per frame and hears less. Self-tests: `python test_pc_adsb_capture.py`, `python test_band_snr.py`.

## Tab5 gate (run on the Tab5 over COM17)

    pwsh apps/orcsdr-tab5/tools/run-tab5-ui-regression.ps1 -Port COM17 -DongleGate V3c -GateIq `
        -VisualDwellSeconds 20 -GateBandwidths 200000,300000,500000,1000000,1800000,2400000,0 `
        -PairingKeyPath <ui-doc.key>

The same gate runs for `-DongleGate V4L` and `-DongleGate V4` (bandwidth stages run only when `-GateBandwidths` is given for those). Opening the port resets the Tab5, so every run is a cold boot. `-WaitForReplug` waits for a real
`RTL_SDR_DISCONNECTED` event and streaming to resume before starting, for the unplug/replug repeat.
The script writes `artifacts/rtl-dongle-gate/<stamp>-v3c.log` and one raw 1 s IQ file per stage
(`<stamp>-v3c-<stage>.cu8`, unsigned 8-bit I/Q at 2.4 MS/s). It never changes gain, bandwidth or
frequency permanently: it restores the initial frequency and bandwidth in a `finally` block.
Copy the script output next to the log as `<stamp>-v3c-console.log` for `cycle_report.py`.

## Analysis

- `cycle_report.py PREFIX [--plot]` — per-stage carrier offset (power-weighted centroid), left/right
  band levels, passband response versus the run's own boot baseline, requested/applied bandwidth,
  tune count, stops/rejected records and drop counters. Register and IF columns are filled only when
  the firmware carries the temporary `V3C_DIAG` readback (never merged).
- `gate_spectrum.py PREFIX [PREFIX_B]` — before/after spectrum figure and carrier table.
- `passband_ratio.py PREFIX` — response versus boot at fixed offsets.

A response ratio is a filter estimate only where the input is the same signal at both times and the
manual gain is constant, which holds within one gate run.

## PC capture (clean-room, black-box)

- `run_pc_bw_capture.ps1` runs USBPcap (all roots, timed autostop) around `pc_bw_stimulus.py`, which
  drives the RTL-SDR Blog `rtlsdr.dll` through its public API only.
- `decode_ep0.py PCAP EVENTS_JSON BUS ADDR OUT` labels the RTL2832U control transfers per stimulus stage.
  The labels are annotations of observed fields, not claims about internals.
