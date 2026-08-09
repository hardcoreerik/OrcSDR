# Tab5 CB dashboard

The CB mode is a receive-only, 40-channel AM/USB/LSB surface. It keeps the live
spectrum and waterfall on the left two thirds of the display and uses the
right third for a stylized CB control panel.

## Controls

- Tap a visible peak in the CB scope to snap to the nearest legal channel.
- Tap around the large dial to select channels 1 through 40.
- Bottom controls are `CH-`, `CH+`, `MODE`, `CLAR`, `SQL-`, and `SQL+`.
- `MODE` cycles AM, USB, and LSB. `CLAR` cycles the sideband clarifier from
  -1.5 to +1.5 kHz in 500 Hz steps. Squelch spans open (`-90 dBFS`) through
  `-35 dBFS` with 3 dB hysteresis. Sound and recording remain global controls.
- The black readout shows channel, frequency, mode, clarifier, squelch, and a
  live S/RF level bar. The
  faceplate and control chrome remain static while the scope renders.

The channel table follows the FCC 40-channel frequency plan. Channel 19
(27.185 MHz) is the default. This UI does not authorize transmission.

## Asset contract

Firmware reads `/orcsdr/cb_dashboard_384x470.jpg` from the Tab5 microSD. The
tracked runtime image is `apps/orcsdr-tab5/assets/cb_dashboard_384x470.jpg`;
the editable full-resolution source is under `docs/cb/dashboard/`.

Install or replace it over the existing USB cable:

```powershell
.\tools\copy_to_tab5_sd.ps1 `
  .\apps\orcsdr-tab5\assets\cb_dashboard_384x470.jpg `
  /orcsdr/cb_dashboard_384x470.jpg -Port COM17
```

The current runtime asset is 49,469 bytes with SHA-256
`40B6F81319219BCCABD0E6BEEA9DB112BF4358357CADB3A240FC1F33B1257F33`.
Firmware draws a simple native fallback if the asset is absent.
