# RTL-SDR Blog V4 observed USB contract

## Scope and provenance

This document records black-box behavior observed from the physical official
RTL-SDR Blog V4 identified as USB `0bda:2838`, serial `00000001`. It is the
only implementation-behavior input permitted for the independent ESP32-P4
implementation, together with public USB and ESP-IDF APIs and the factual
hardware documentation listed below. Do not consult, translate, or copy an
existing RTL-SDR driver while implementing this contract.

Two independent Windows sessions ran the same bounded command:

```text
rtl_sdr -f 100000000 -s 2400000 -n 2400000 <output>
```

Both sessions detected the R828D and RTL-SDR Blog V4, selected automatic
gain, tuned to 100 MHz, and produced exactly 4,800,000 unsigned interleaved
I/Q bytes. The ordered request sequence was identical across both sessions.
Six returned register/status values differed, at zero-based sequence indices
`378`, `450`, `456`, `458`, `489`, and `496`; those values did not alter any
subsequent request.

Evidence hashes:

- Trace A SHA-256: `6ED5D3C245D9D8A8610A70D15830443E6C02F1A264F7EA5AE976655BD8E2E8D4`
- Trace B SHA-256: `564AB84E5618DBCA757A2CADF7BBA94C7062FF7AF0B6B9AA23B8FCF5649AD9C5`
- Reference IQ SHA-256: `0A9636CEC037952A858DF907BCC6AE999A84FE3FD9893E78DA518FB2627CBECB`
- Compact transfer artifact: `rtl_sdr_v4_clean_room_transfers.json`

The packet captures and IQ recording remain local under
`.orclink/clean-room/`; they are evidence, not distributable source inputs.

## USB contract

- USB 2.0 device, configuration `1`.
- Interface `0` is vendor-specific and owns bulk IN endpoint `0x81`.
- Endpoint `0x81` has a 512-byte maximum packet size.
- Interface `1` has no endpoint and is not needed for the bounded capture.
- All observed device-control operations use `bRequest = 0`.
- Writes use `bmRequestType = 0x40`; reads use `bmRequestType = 0xC0`.
- The reference host submitted 262,144-byte bulk reads. A smaller aligned
  transfer size is acceptable on ESP32-P4 as long as the byte stream remains
  continuous and the requested bounded byte count is reached.
- Both reference sessions returned `STALL` at the same six zero-based
  initialization indices, `86` through `91`, while probing absent tuner I2C
  addresses `0xC8`, `0xC6`, and `0x34`. Those six STALLs are expected; the
  following probe of the V4's R828D at `0x74` succeeds.

The JSON artifact contains 515 initialization operations followed by 16
cleanup operations. Each operation is one `|`-delimited record with fields:

```text
bmRequestType|wValue|wIndex|wLength|out_data|observed_in
```

All hexadecimal values omit the `0x` prefix. `out_data` is populated only for
host-to-device operations; `observed_in` is reference evidence and must not be
treated as a fixed value unless independently validated. Except for the six
explicitly measured tuner-probe STALLs, a control transfer failure is fatal;
do not silently continue after timeout, stall, disconnect, or short data.

## Minimal implementation

1. Accept only the measured `0bda:2838` V4 identity.
2. Select configuration `1` and claim interface `0`.
3. Execute the 515 initialization records in order.
4. Read endpoint `0x81` until exactly 4,800,000 bytes have been received or a
   fixed timeout expires.
5. Report byte count plus simple evidence that the stream is not constant:
   minimum, maximum, arithmetic mean, and a SHA-256 digest.
6. Execute the 16 cleanup records after success or best-effort after a stream
   failure while the device is still present.
7. Release the interface without affecting the existing Tab5 console,
   journal, touch, display, or authenticated serial protocol.

The first milestone is intentionally fixed at 100 MHz, 2.4 MS/s, automatic
gain, and one bounded capture. Frequency formulas, gain controls, continuous
streaming, HF switching, bias tee, and demodulation require additional
independent traces and are not implied by this contract.

## Physical validation status

- Firmware `0.8.4` cold-cycled the Tab5 USB-A rail, re-enumerated the V4 as a
  high-speed device, and then received `USB_TRANSFER_STATUS_STALL` on the
  first observed vendor OUT request.
- Firmware `0.8.5` retried that request after ESP-IDF automatically cleared
  and reactivated EP0; the V4 STALLed the retry too.
- Firmware `0.8.7` issued an observed read-only vendor IN request before the
  initialization sequence; both attempts STALLed. Standard enumeration and
  string-descriptor control requests continue to succeed.
- Firmware `0.8.10` sent a real 18-byte standard device-descriptor request
  through the exact same client transfer wrapper used for the captured V4
  records. The standard request completed with the expected 26 total bytes;
  the immediately following observed vendor IN request STALLed twice. This
  rules out the wrapper's allocation, EP0 packet rounding, submission,
  callback, and automatic STALL-clear path.
- Firmware `0.8.11` restored the captured initialization order and completed
  the first 86 vendor transfers before STALLing at sequence index `86`, the
  `0xC8` absent-tuner probe. Both PC traces return the same STALL at that exact
  operation and continue; this exposed the missing expected-outcome metadata
  in the first compact transfer artifact.
- Firmware `0.8.12` accepted only the six independently observed tuner-probe
  STALLs, completed all 515 initialization transfers, and captured exactly
  4,800,000 live I/Q bytes from the V4 directly attached to the Tab5 USB-A
  port. The stream spanned `0` through `255`, had mean `127.428`, and SHA-256
  `6ea5bc55b11ac18040fdf79c2bb2b749542964331ec729c2a8e5c7f0f690a39d`.
- A firmware `0.8.6` USB-A power-cycle attempt triggered the ESP32-P4
  brownout detector. The V4 recovered and re-enumerated after the Tab5 reboot.
- Automatic USB-A rail cycling was removed after that brownout; capture
  diagnostics no longer toggle the Tab5's `USB5V_EN` output.
- The captured V4 configuration descriptor declares `bMaxPower = 500 mA`.
  The official V4 datasheet lists 250–270 mA typical current draw.
- The official Tab5 schematic shows USB-A VBUS (`SYS_USB5V`) behind an
  MT9700 switch (U27) with a fixed 5.1 kΩ SET resistor (R84). The MT9700
  datasheet defines the typical threshold as `6.8 / RSET(kΩ)` amperes, so
  this branch is hardware-set to about 1.33 A. There is no firmware current
  limit to raise, and the branch threshold is already well above the V4's
  documented typical draw.
- The reference PC capture shows `SET_CONFIGURATION(1)`, the normal string
  descriptor reads, and then the first vendor request. It contains no
  `SET_INTERFACE` or other device-specific control precondition between
  enumeration and the captured initialization sequence.

The direct `0.8.12` capture rules out a steady-state USB-A power shortage and
an ESP32-P4/V4 control-transfer incompatibility for this device. A powered hub
is not required for the verified path. Keep automatic USB-A rail cycling
disabled; it adds brownout risk without helping normal initialization.

## Fixed KZEL preset and live DSP evidence

The independently observed 100 MHz final-tune state writes R82xx registers
`R16=0x84`, `R20=0xCA`, `R21=0x5A`, and `R22=0x90`. The public register
description defines `Nint = 4*NI2C + SI2C + 13`, a 16-bit fractional word, and
`Ndiv = 2*(Nint + Nfra)`. With the observed divider retained, that state
resolves to a 101.814972 MHz local oscillator. Subtracting the requested
3.9 MHz center-frequency delta yields `R20=0x4A`, `R21=0xAF`, and `R22=0x65`,
or 97.914963 MHz (9 Hz rounding error). The nearby observed tracking-filter
state is retained. This is an independently calculated fixed preset, not a
copied tuner implementation.

- Firmware `0.8.13` replayed the observed final-tune transaction with only
  those three PLL bytes changed. All 22 preset transfers completed and the
  Tab5 captured 4,800,000 nonconstant I/Q bytes.
- Firmware `0.8.14` added a minimal mono WBFM path: 10:1 complex boxcar
  decimation, quadrature phase discriminator, channel low-pass, 5:1 audio
  decimation, 75 us de-emphasis, DC removal, and 48 kHz ES8388 speaker output.
  The physical run produced 47,999 audio samples with zero dropped buffers.
- Firmware `0.8.15` exposed a stack fault from keeping the FFT scratch arrays
  on the USB task stack. Firmware `0.8.16` moved only that 1.5 KB scratch area
  to static storage; no task-stack increase was needed.
- Firmware `0.8.16` completed the full physical display/audio run: 24,000,000
  I/Q bytes, mean `127.384`, SHA-256
  `193b57fd6e5133d058cf7df203c5dcbd741b8288ff4c587c7b561339447adad2`,
  239,999 audio samples, peak `11833`, RMS `3783.4`, 733 queued audio chunks,
  and zero dropped chunks. The live display uses a 128-bin FFT, a 48 dB
  spectrum trace, and a scrolling color waterfall centered on 96.1 MHz.

The control transfers, bulk stream, DSP output, and renderer completion are
runtime-observed. Station identity and subjective audio quality still require
operator confirmation from the physical Tab5 speaker.

## NOAA preset, continuous operation, and real-time rate

The fixed NOAA KEC42 preset uses the same independently observed final-tune
template. For 162.400 MHz, the calculated 164.214972 MHz local oscillator uses
divider `/16`, `R16=0x84` during setup and `R16=0x64` when active, plus
`R20=0x08`, `R21=0x82`, and `R22=0x9D`. The resulting oscillator is
164.214954 MHz, an error of -18.3 Hz.

- Firmware `0.8.17` completed a physical NOAA run with 24,000,000 I/Q bytes,
  239,999 audio samples, and zero dropped speaker chunks.
- Firmware `0.8.19` added authenticated `RTL_LISTEN` and `RTL_STOP` commands
  plus the equivalent KZEL, NOAA, and stop touch controls. The first timed
  session correctly exposed the five-second authentication lease; one-second
  adapter PINGs fixed the session without weakening authentication, and an
  authenticated stop recovered the still-running stream without a reboot.
- Firmware `0.8.20` measured 1,540,224 samples/s at the observed 2.4 MS/s
  setting after caching the FFT window, reducing display refresh work, and
  using a bounded-error phase approximation. This proved that single-buffer
  DSP left audible gaps at that rate.
- Firmware `0.8.21` applied the independently derived RTL2832 rate ratio for
  1.024 MS/s (`0x07080000`). The V4 accepted the control records, but the
  complete display/audio path sustained only 816,336 samples/s.
- Firmware `0.8.22` tested 768 kS/s and produced 280,785 samples/s, confirming
  that this request falls in the RTL2832's unusable mid-rate region. That
  experimental build was replaced and is not the approved device image.
- Firmware `0.8.23` uses the valid 960 kS/s ratio
  `28.8 MHz * 2^22 / 960000 = 0x07800000`. One 32 KB static scratch buffer lets
  the USB endpoint refill while the P4 processes the preceding block; no extra
  task, queue, or driver dependency is required. A timed NOAA run sustained
  940,504 samples/s (98.0% of requested), generated 216,268 audio samples in
  4.599 seconds, queued 264 speaker chunks, and dropped none. A bounded KZEL
  run captured exactly 9,600,000 bytes at 939,702 samples/s, generated 239,999
  audio samples, dropped no chunks, and produced SHA-256
  `7073b4204f0ebdc8f45f319e29468ae97e147fdee9f5285cc9d3f90d68916c81`.

Firmware `0.8.23` is the last clean-room ladder image with full runtime
artifact notes in this document. Continuous stop behavior, both tuner presets,
bulk I/Q, DSP production, speaker queueing, and the spectrum/waterfall render
path are runtime-observed. Operator confirmation (2026-08-06): KZEL and NOAA
audio are audible on the physical Tab5 (volume was reported quiet).

Repo work after 0.8.23 (volume UI, FM/AM/WX band flow, higher-rate spectrum)
is tracked in `M5TAB5_RTL_RADIO_NEXT_STEPS.md` and must not be described as a
new clean-room measurement until re-flashed and logged on the unit.

## Public factual references

- Official V4 user guide: <https://www.rtl-sdr.com/v4/>
- Official V4 datasheet: <https://www.rtl-sdr.com/wp-content/uploads/2024/12/RTLSDR_V4_Datasheet_V_1_0.pdf>
- Official V4 release notes: <https://www.rtl-sdr.com/rtl-sdr-blog-v4-dongle-initial-release/>
- Official RTL-SDR quick start: <https://www.rtl-sdr.com/rtl-sdr-quick-start-guide/>
- Public R820T2 register description: <https://www.rtl-sdr.com/wp-content/uploads/2016/12/R820T2_Register_Description.pdf>
- Official M5Stack Tab5 hardware documentation: <https://docs.m5stack.com/en/core/Tab5>
- Official M5Stack Tab5 schematic: <https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf>
- Aerosemi MT9700 datasheet mirror: <https://www.demsaystore.com/Data/EditorFiles/Datasheets/MT9700_AEROSEMI.pdf>

The supplied USB-C modification project, third-party chip-datasheet mirrors,
and community wiki are useful orientation, but they are not implementation
inputs for this driver milestone.
