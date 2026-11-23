# First-party NXDN architecture

```text
Existing radio owner's 48-kS/s IQ channel (future bounded, read-only tap)
  → Frontend NXDN48 (2400 sym/s) or NXDN96 (4800 sym/s)
    → Kaiser channel-select IQ FIR → continuously tracked carrier NCO
    → quadrature discriminator → RRC matched FIR
    → interpolated Mueller-Muller timing → residual symbol DC and 4-level slicer
    → stream of on-air dibits 0..3
  → Decoder fixed 192-dibit window
    → 20-bit FSW 0xCDF59 → PN9 x^9+x^5+1 seed 0xE4
    → eight-dibit LICH parity/filler check
       ├── candidate frame only (RFCT/FCT/direction, raw VCH positions)
       └── traffic-associated 60-bit SACCH → 12x5 deinterleave
           → insert 12 puncture erasures → K=5 (031/027) Viterbi
           → zero tail + CRC6 preset 0x3f/poly 0x27
           → six-bit CRC-validated RAN event
```

PN9 post-FSW runs for all 182 dibits. Frame layout: FSW 10 / LICH 8 / SACCH 30 / information field 144 dibits. Voice field holds four 36-dibit codewords with LICH steal-mask selection. These are **unverified 72-bit vocoder candidate blocks**: 49-bit extraction, Golay, voice PCM and licensing are not implemented. A candidate frame NEVER establishes a RAN, radio identity or system. RAN only after SACCH CRC.

RFCT 0 is RCCH control; RFCT 1/2/3 is traffic/composite. RCCH LICH recognition is not CAC/Type-C grant validation. Type-D distributed SCCH is a separate independent track. Frontend/decoder state must be reset on retune/discontinuity, with no new USB owner or UI/main.cpp modifications. Max FSW tolerance is configurable: default exact, replay one damaged dibit only with downstream evidence checks.

This pipeline is a host demonstration; ESP32-P4 performance, heap/stack and real-air acquisition remain gated.