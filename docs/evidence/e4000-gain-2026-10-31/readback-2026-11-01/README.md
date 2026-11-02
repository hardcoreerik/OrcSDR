# E4000 physical gain readback, 2026-11-01

Approved one build/COM39 flash of a00e1305eae5fb46f1c440f94d8a4d5e470df658.
Managed driver source compared with local commit; image/provenance.json records
5,444,944 bytes, SHA-256 1300a7672507683da221713aa71d942ea5159e7c1e960ac4089b7ff7c1f04b31.
Esptool write hashes verified, intentional reboot. Previous image retained.
No access to COM17/COM8. Existing isolated HAL/M5GFX retained.

Fixture unchanged XTR COM39, tinySA 100.1 MHz nominal -40 dBm through 30 dB
attenuator; receiver 100 MHz/2.4 MS/s. Source-off then 14/16.5/14 dB and
AUTO. Each capture stops for retrieval; next capture restarts reception.
Five 4.8 MB CU8 captures matched device hashes; no rail clipping. Run exit0,
source OFF/pause acknowledged at completion, manual -1 dB restored.

73 recorded E4000_GAIN_REG lines matched expected controlled masks.
Not all logs from the run are guaranteed retained: Tab5 authentication reader
can consume unrelated lines. Count describes archived observations only.
Immediate readback does not prove fields persist through later calibration.

Integrated Hann +/-4 bin tone results: 14=-27.40853, 16.5=-28.08451,
return14=-29.26625, AUTO=-8.47560 dBFS. Dip -0.67598 dB, return shift
-1.85772 dB. AUTO retained tone without clipping at one level; dynamic response
unqualified. Neither IF baseline nor immediate write verification fixed RF
repeatability. Do not merge/tag this as an accepted RF correction.

Next compare full E4000 state and RTL2832U demod gain/AGC across start/retune
against PC initialization. First distinguish a live gain change from receiver
restart history. Avoid further speculative gain-table changes.
Raw IQ/ELF/bin remain ignored build artifacts. Archived runner retains local
paths; fresh output dir and same fixture required to repeat. results.json
contains raw hashes and exact integrated results. events.json stores serial
commands and observed register lines without authentication secrets.
