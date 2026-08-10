# Last pre-restart recording analysis

Source on Tab5: `/orcsdr/rec_001_FM_91900000.wav`

The file was retrieved through the USB SD readback protocol and its local
SHA-256 matched the device value:
`1F3A03825137CDA3F9D42C9671397D31F0D831F32F5D40F7D64DBCA3B86466A7`.

## Results

| Check | Result |
|---|---:|
| Container | Valid RIFF/WAVE; one RIFF header |
| Format | Mono PCM, signed 16-bit, 48 kHz |
| Duration | 12.000 s (576,000 samples) |
| File/data sizes | Exact: 1,152,044 / 1,152,000 bytes |
| Peak | -10.69 dBFS |
| RMS | -20.62 dBFS |
| Crest factor | 9.93 dB |
| Clipped samples | 0 |
| Longest exact-zero run | 1 sample / 0.021 ms |
| DC offset | -13 samples (-0.04% full scale) |

No truncation, appended RIFF container, clipping, or digital dropout was
detected. About 79.6% of measured 20 Hz-20 kHz energy is in 80 Hz-4 kHz and
19.6% is below 80 Hz; only 0.7% is above 4 kHz. The recording is therefore
technically intact but strongly low-frequency/voice-band weighted.

The recorder previously opened colliding post-reboot sequence names in append
mode. Firmware now removes an existing destination before writing a fresh WAV,
preventing future multi-RIFF collisions.
