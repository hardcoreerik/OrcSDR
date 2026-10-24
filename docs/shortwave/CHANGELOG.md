# Shortwave Explorer Changelog

Changes to the Shortwave Explorer dashboard. Newest first.

## Unreleased

### Added

- **USB and LSB listening on the Shortwave Explorer** (`shortwave_ssb`): the screen previously demodulated AM only and merely advised "USB/LSB" for amateur segments.
  A new MODE button cycles AUTO, AM, USB and LSB. AUTO follows the band plan: LSB for the amateur voice segments below 10 MHz (160, 80, 40 m), USB above (30, 20, 17, 15, 12, 10 m) and for the 60 m channels, AM everywhere else.
  The mode in force shows on the button, in the `RTL_FREQ` mode field, and in saved memories and log entries (previously fixed to "AM").
- **Sideband filter and clarifier:** the SSB path is its own module (a 2.7 kHz voice passband, eight-pole, with the opposite sideband rejected by more than 40 dB in the host test) rather than the CB sideband code, which mixes at a fixed offset and has no sideband selectivity.
  A CLAR - / + pair (50 Hz steps, up to 1.5 kHz either way) trims the pitch; raising the dial lowers USB pitch and raises LSB pitch, as on a transceiver.
- **Direct-sampling fold:** above 14.4 MHz the RTL-SDR's 28.8 MHz direct-sampling path folds the spectrum back inverted, so the sideband is swapped there. The correction is in `shortwave_ssb::demod_sideband` and is covered by the host test; see the pull request for what was and was not verified on air.

### Changed

- **Home accepts any frequency the attached dongle can tune.** The Home keypad was fixed at 24 - 1766 MHz; it now follows the dongle (from 24 kHz on a V3/V4 with direct sampling or an upconverter, 24 MHz on a dongle without HF). A frequency below 24 MHz opens on the Shortwave band, so AM/USB/LSB, the HF filters and the band-plan AUTO mode apply. The OrcDial keypad uses the same rule.
- **Speaker volume uses the full 0-255 range on every band except P25.** The physical speaker level was capped at 200, so the top of the volume slider did nothing; the cap now applies only to P25, whose full-scale voice burst tripped the amplifier's rail detector. SSB audio gets about 10 dB more gain than before, because it has no carrier and was much quieter than AM at the same setting.
- The Shortwave control row is re-spaced to make room for MODE (STEP, FILTER and the other buttons are narrower).
- In SSB the FILTER readout shows the fixed 2.7 kHz passband; the AM width presets do not apply.
- Shortwave memories and log entries accept the modes AM, USB and LSB (existing AM records remain valid).
- The Shortwave mode guide marks LSB and USB segments as supported instead of "GUIDE".

### Not changed

- The SSTV receiver on the Shortwave band keeps its own demodulator.
- The sideband state and its control block live in PSRAM; the internal RAM boot pool is not touched.
- The mode choice is not saved across reboots (it starts in AUTO).
