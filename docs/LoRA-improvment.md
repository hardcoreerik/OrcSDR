# LoRa Capture Improvement

Status: deferred. This is not required for the current RC2 candidate.

## What OrcSDR Does Today

While the LoRa dashboard is open, OrcSDR continuously receives IQ samples and
checks them for likely LoRa activity. It does not retain the entire IQ stream.
When activity is detected, it copies a limited window into one of two buffers
and decodes that recording.

This allows one buffer to capture while the other is being decoded. However,
if decoding holds both buffers, another transmission can appear on the
spectrum and waterfall without being retained for decoding.

At 960 kSPS, raw 8-bit complex IQ consumes about 1.92 MB per second. A
four-second capture therefore needs about 7.68 MB. Unlimited recording in
memory is not possible, and continuous SD-card recording would introduce a
second throughput bottleneck.

## Deferred Improvement

Separate receiving from decoding with a bounded circular IQ recorder:

1. Continuously retain the most recent IQ samples in memory.
2. Mark a possible frame start when the detector sees a preamble or suitable
   energy rise, while preserving pre-roll samples.
3. Mark the capture end after the signal's quiet tail.
4. Queue the bounded frame for later decoding.
5. Immediately continue recording while older frames are decoded.
6. Report queue depth and dropped frames explicitly if decoding falls behind.

The energy boundary is only a recording boundary. Noise, interference, and
overlapping transmitters mean it cannot by itself prove the exact beginning or
end of a valid LoRa packet. The decoder must still locate and validate the
packet within the retained recording.

## Constraints

- Keep the current 960 kSPS rate unless measurements prove another rate is
  necessary.
- Do not depend on continuous SD-card writes for normal reception.
- Bound memory use and define an explicit policy for a full queue.
- Preserve enough pre-roll and post-roll for SF11/BW250 and slower supported
  configurations.
- Keep receiving active while decoding and exporting Record IQ files.
- Do not change tuner registers or RF gain as part of this buffering work.

## Evidence to Record Before Implementation

- Capture and decode time for short, maximum-length, and false-trigger windows.
- Maximum sustainable packet rate on the Tab5.
- Required buffer duration for supported spreading factors and bandwidths.
- Whether missed packets were never retained, rejected during preamble/header
  detection, or failed CRC.
- Results with a suitable 915 MHz antenna and controlled numbered messages.

## Acceptance Test

At 906.875 MHz, SF11, BW250k, and with a suitable 915 MHz antenna, send 20
numbered messages at controlled intervals. Compare the number delivered to the
mesh with OrcSDR's trigger, retained-frame, preamble, header, CRC, displayed,
and dropped-frame totals. Natural telemetry and GPS packets should be counted
separately.
