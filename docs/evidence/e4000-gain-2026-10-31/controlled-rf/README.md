# Controlled E4000 gain run, 2026-11-01

Fixture: tinySA Ultra COM7 (USB serial 400), RF output -> 30 dB attenuator ->
Nooelec SMArt XTR serial 82115932, USB on Tab5 COM39. No antenna or upconverter.
CW 100.1 MHz, nominal source -40 dBm, receiver 100 MHz at 2.4 MS/s.
Source level and attenuation are nominal, not a calibrated power measurement.
Firmware and driver provenance are recorded in the parent directory.

The two Python scripts are exact archives of the scripts used, including local
paths. To repeat from the OrcSDR repository root, install numpy and pyserial,
check COM39/COM7 identities and the RF fixture, then adjust `root` (must be a
new output directory), `source` (path to the archived tinySA helper), and the
local authentication-key path in rf_gain_matrix.py. Never commit key content.
Run `python path/to/rf_gain_matrix.py`. Do not open competing serial monitors.
This starts/stops reception and changes gain; it does not flash firmware.

The source is paused before setting CW frequency. A finally block commands
output off and pause; check the final acknowledgments in events.json. The
helper checks tinySA USB identity and requires a prompt for each command.
A disconnected instrument still requires physical verification of output state.

Each capture retrieves 4,800,000 CU8 bytes and verifies the device SHA-256.
The analysis averages 16 Hann-windowed 65,536-point FFT powers, finding the
peak within +/-3 kHz of +100 kHz. Manual captures require frequency within
2 kHz, tone >15 dB above the source-off search peak, and rail fraction <0.0001.
AUTO checks frequency and rail fraction but is not an AGC response-time test.
Raw CU8 remains in the ignored build directory; the JSON records its hashes.

The PC reference is the accepted `tinysa-paused-matrix/events.json` capture
under C:/Tools/esp-rtl-sdr-lab/captures/e4000-xtr-20261031. Earlier runs using
`freq` rather than paused `sweep cw` did not establish a valid tone and are
excluded. Different capture lengths, initialization and offset settings mean
this is a reference comparison, not a calibrated gain-accuracy acceptance test.
