# AM broadcast research and provenance

This record distinguishes research from incorporated source code.

| Source | License / terms | Learning used | Derivation status |
|---|---|---|---|
| [FCC 88-82](https://docs.fcc.gov/public/attachments/FCC-88-82A1.pdf) | United States government record | Region 2 AM planning extends the existing medium-wave band and must account for adjacent-channel interference. OrcSDR retains selectable channel spacing and receiver bandwidth. | Requirements reference only; no source code. |
| [Osmocom `rtl_power`](https://github.com/osmocom/rtl-sdr/blob/master/src/rtl_power.c) | GPL-2.0-or-later | A stepped frequency range with dwell/measurement is an established SDR scanning workflow. | Conceptual cross-check only. OrcSDR reuses its independently implemented `scan_engine`; no Osmocom code or constants were copied. |
| [Osmocom RTL-SDR API](https://github.com/osmocom/rtl-sdr/blob/master/include/rtl-sdr.h) | GPL-2.0-or-later | Tuner automatic and manual gain are distinct modes; manual gain must be enabled before selecting a gain step. | API behavior cross-check only; OrcSDR uses its own ESP driver API. |
| [RTL-SDR Blog V4 datasheet](https://www.rtl-sdr.com/wp-content/uploads/2024/12/RTLSDR_V4_Datasheet_V_1_0.pdf) | Vendor documentation | The V4 HF path has adjustable gain, so medium-wave gain policy belongs in OrcSDR rather than being treated as a fixed front end. | Hardware reference only; no source code. |

The first AM station scan is deliberately receiver-relative: it measures every
channel, estimates the median local RF floor, and offers prominent local peaks.
It fills empty preset slots only, so a scan cannot erase a user's saved stations.

## AM gain acceptance evidence

On the development receiver with a powered ML30+ antenna, a 1050 kHz sweep
showed input power flattening above roughly 25 dB of tuner gain. Listening tests
then found that 1280 kHz sounded best at 0.0 dB; a nearby off-channel case was
clearer around 8.7 dB. These results reject maximum signal strength and the
tuner's unbounded hardware AGC as AM quality criteria.

OrcSDR's first bounded AM Auto policy therefore starts at the lowest measured
tuner step and advances only while the live input remains below -24 dBFS.
Manual movement disables Auto immediately. This is a receiver-protection and
usability heuristic, not a claim that firmware can measure perceived clarity;
further antenna and station listening remains the acceptance gate.
