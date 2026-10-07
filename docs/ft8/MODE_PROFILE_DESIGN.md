# Weak-Signal Mode Profile Design

Status: **PROPOSAL — owner review required before implementation**

Branch: `codex/ft8-native-decoder-research`

Baseline: `claude/ft8-ui` at `804268c71af127490364c325a76e61383042f137`

This document defines the decoder-owned mode abstraction for FT8, FT4, and JS8. It does not change the UI-owned dashboard/model files.

## Design goals

One analysis engine should handle modes that share the same broad receive problem without pretending that their framing or FEC is identical.

The reusable layers are:

```text
12 kHz mono USB analysis PCM
          |
          v
incremental spectral analysis
          |
          v
ModeProfile-driven sync search
          |
          v
ModeProfile-driven tone-energy extraction
          |
          v
mode-specific soft-bit mapping / FEC
          |
          +--> FT8/FT4: 77-bit FTX message family
          |
          +--> JS8: 75-bit JS8 frame family
                       |
                       v
                 multi-frame assembler
```

The abstraction is deliberately data-oriented. Search, demodulation, candidate ranking, and scheduling consume a profile; they do not branch through a second copy of the decoder for each mode.

## Verified mode facts

### FT8

Primary sources: Franke/Somerville/Taylor, "The FT4 and FT8 Communication Protocols," QEX July/August 2020, and the WSJT-X protocol specification.

- T/R period: 15 s.
- Analysis sample rate used by the protocol definitions: 12 kHz.
- Symbol samples at 12 kHz: 1920.
- Symbol time: 0.160 s.
- Keying rate / tone spacing: 6.25 baud / 6.25 Hz.
- Modulation: 8-GFSK.
- Payload: 77 bits.
- CRC: 14 bits.
- FEC: LDPC(174,91), 83 parity bits.
- Data symbols: 58, three coded bits per symbol.
- Sync: three 7-symbol Costas blocks.
- Total channel symbols: 79.
- Signal duration: 12.64 s.
- Nominal occupied bandwidth: 50 Hz.
- Costas sequence: 3,1,4,0,6,5,2.
- Sync positions: 0..6, 36..42, 72..78.

### FT4

Primary sources: the same QEX paper and current WSJT-X protocol specification.

- T/R period: 7.5 s.
- Symbol samples at 12 kHz: 576.
- Symbol time: 0.048 s.
- Keying rate / tone spacing: 20.833333... baud / 20.833333... Hz.
- Modulation: 4-GFSK.
- Payload/FEC family: same 77-bit payload, CRC-14, LDPC(174,91) as FT8.
- Coded-data symbols: 87, two coded bits per symbol.
- Sync: four distinct 4-symbol Costas blocks.
- Ramp-up/ramp-down: one symbol at each end.
- Total channel symbols: 105.
- Signal duration: 5.04 s.
- Nominal occupied bandwidth: 83.3 Hz.
- Costas arrays documented by the QEX protocol resources:
  - 0,1,3,2
  - 1,0,2,3
  - 2,3,1,0
  - 3,2,0,1
- The assembled 77-bit FT4 payload is XOR-whitened with the protocol-defined pseudo-random sequence **before** CRC and FEC, then XORed again after receive decode to restore the message.

The profile therefore cannot model FT4 as merely "FT8 with four tones." It needs a payload transform and different frame layout.

### JS8

Sources: current JS8Call user guide plus the current `js8call/js8call` GPL-3.0 source **read only for protocol/framing understanding**. No JS8Call implementation code or control flow is to be copied into OrcSDR.

The current guide describes four stable receive speeds and an experimental fifth speed. Current source constants at 12 kHz agree with the published stable modes:

| Profile | Slot period | Symbol samples | Symbol time | Baud / spacing | 8-tone bandwidth | 79-symbol TX duration |
|---|---:|---:|---:|---:|---:|---:|
| JS8 Slow | 30 s | 3840 | 0.320 s | 3.125 | 25 Hz | 25.28 s |
| JS8 Normal | 15 s | 1920 | 0.160 s | 6.25 | 50 Hz | 12.64 s |
| JS8 Fast | 10 s | 1200 | 0.100 s | 10 | 80 Hz | 7.90 s |
| JS8 40 | 6 s | 600 | 0.050 s | 20 | 160 Hz | 3.95 s |
| JS8 60 / Ultra | 4 s | 384 | 0.032 s | 31.25 | 250 Hz | 2.528 s |

**JS8 60 is experimental.** The current user guide explicitly says its specification is unpublished and may change. OrcSDR should carry the profile only as an experimental/disabled definition until a stable protocol source exists and interoperability tests are available.

JS8 Normal uses the original three 7-symbol FT8-family Costas blocks. Stable non-Normal submodes use modified, distinct Costas blocks for improved synchronization.

Important FEC distinction: current JS8Call source documents a **75-bit message plus CRC-12 = K=87** protected in an N=174 codeword. That is not FT8/FT4's K=91 code. Shared spectral and demodulation code is appropriate; blindly reusing the FT8 LDPC graph is not.

The JS8 protocol layer classifies frames into at least:
- heartbeat;
- compound callsign partial;
- compound callsign directed command;
- directed command;
- data Huffman;
- data dictionary.

Frames also carry transmission state such as first/last. Free text and directed traffic can span multiple frames, so receive support requires a separate bounded assembly layer.

## Proposed C++ representation

Suggested decoder-owned file: `apps/orcsdr-tab5/ui/ft8_mode.hpp` initially, with a future rename to a broader `weak_signal_mode.hpp` only if that improves repository naming.

```cpp
namespace orcsdr::ftx {

enum class Mode : uint8_t {
  ft8,
  ft4,
  js8_normal,
  js8_fast,
  js8_40,
  js8_slow,
  js8_60_experimental,
};

enum class CodeFamily : uint8_t {
  ftx_77_crc14_ldpc174_91,
  js8_75_crc12_ldpc174_87,
};

enum class PayloadTransform : uint8_t {
  none,
  ft4_xor,
};

enum class SyncFamily : uint8_t {
  ft8_costas_7,
  ft4_costas_4,
  js8_original_7,
  js8_modified_7,
};

struct SyncBlock {
  uint16_t first_symbol;
  uint8_t length;
  uint8_t tones[7];
};

struct ModeProfile {
  Mode mode;
  const char* name;

  uint32_t sample_rate_hz;       // 12000 for all initial profiles
  uint32_t slot_ms;
  uint16_t symbol_samples;
  uint16_t channel_symbols;
  uint16_t data_symbols;

  uint8_t tone_count;
  uint8_t bits_per_tone;
  uint32_t tone_spacing_millihz;

  uint8_t sync_block_count;
  SyncBlock sync[4];

  CodeFamily code_family;
  PayloadTransform payload_transform;

  bool has_ramp_symbols;
  bool experimental;
};

const ModeProfile& profile(Mode mode);
bool profile_valid(const ModeProfile& profile);
}
```

Why integer spacing rather than float? Profile constants are protocol identity, not DSP state. Millihertz represents all current rates exactly enough for configuration and avoids making equality/configuration tests depend on floating-point rounding. DSP code may derive float/NCO increments from these constants.

## Shared-core boundaries

### Shared by FT8, FT4, and JS8

- 12 kHz PCM chunk ingestion.
- rolling/incremental spectral storage.
- time-frequency candidate representation.
- local noise normalization.
- generic Costas/matched-sync scoring driven by SyncBlock definitions.
- candidate deduplication/non-max suppression.
- fine DT/frequency refinement.
- tone-energy extraction for N-tone orthogonal FSK.
- CPU/deadline accounting.

### Shared only by FT8 and FT4

- 77-bit source-message pack/unpack.
- CRC-14.
- LDPC(174,91) generator/parity graph and soft decoder.
- callsign/hash/grid/report message family.

FT4 additionally applies its payload XOR transform and its own 2-bit/tone Gray mapping/frame layout.

### Separate JS8 layer

- 75-bit frame payload.
- CRC-12.
- JS8 LDPC(174,87) code definition.
- JS8 frame type and directed-message fields.
- text/dictionary/Huffman or successor data encoding.
- frame provenance.
- bounded multi-frame assembly.
- conversations/message output.

This distinction prevents "shared core" from becoming accidental protocol conflation.

## Result types

The decoder core should have an internal mode-tagged result independent of the current dashboard Decode record:

```cpp
struct FrameResult {
  Mode mode;
  uint32_t utc_epoch;
  int16_t dt_ms;
  uint16_t audio_hz;
  int16_t snr_db;
  int16_t sync_score;
  uint16_t flags;       // normal/assisted/hash-resolved/etc.
  char text[64];
};
```

Only CRC/FEC-valid and legally unpacked results can leave the core as FrameResult.

For JS8, frame results additionally feed a bounded assembler. A proposed UI-facing assembled record is documented in `DECODER_SEAM_PROPOSALS.md`.

## Slot timing

The decoder must not hard-code 15 seconds globally.

The runtime chooses the selected Mode; the ModeProfile owns slot duration. A valid wall clock establishes the coarse slot boundary. Sync search estimates fine DT inside the selected period.

Examples:
- FT8: floor(UTC_ms / 15000).
- FT4: floor(UTC_ms / 7500).
- JS8 Fast: floor(UTC_ms / 10000).
- JS8 Slow: floor(UTC_ms / 30000).

JS8 protocol interoperability must be tested against real current JS8Call recordings because JS8's own documentation warns that implementation details remain active-development material.

## Search scheduling consequence

Profiles have very different symbol lengths. The incremental analyzer should schedule work by **received symbol count**, not arbitrary wall-clock polling.

A 30-second Slow JS8 slot does not justify a 30-second CPU burst at the end. Conversely, JS8 40 has 600-sample symbols and demands faster incremental updates than FT8.

The pipeline should therefore expose a per-profile "analysis quantum" derived from symbol_samples and process bounded chunks while samples arrive.

## Memory consequence

The whole framework should avoid allocating one worst-case float spectrogram for every mode simultaneously.

Only the active profile owns:
- one rolling analysis history;
- one bounded candidate set;
- one FEC workspace for the active code family;
- one residual/subtraction buffer if that feature is enabled.

JS8 assembly state is separate, small, and persists across RF frames with explicit expiration.

## Test plan

Before a profile is enabled:
1. static profile invariants;
2. exact symbol/frame vectors from independently documented protocol facts;
3. synthetic waveform fixtures at the declared rate/spacing;
4. sync acquisition sweeps across DT/frequency/drift;
5. end-to-end decode with CRC/plausibility;
6. external interoperability measurement against an authoritative implementation;
7. false-accept corpus;
8. host and Tab5 CPU/RAM/deadline measurements.

## Decisions requested

1. Approve one shared `ModeProfile` framework rather than separate FT8/FT4/JS8 decoders.
2. Approve two FEC families: FT8/FT4 share (174,91); JS8 gets a separate (174,87) family.
3. Approve JS8 60 as defined-but-disabled/experimental until its specification stabilizes.
4. Approve a separate JS8 multi-frame assembler rather than forcing conversations into the 48-byte FT8 Decode string.
5. Approve integer/rational protocol constants in the profile, deriving float DSP coefficients at runtime/init time.

## Sources

- Franke, Somerville, Taylor, "The FT4 and FT8 Communication Protocols," QEX July/August 2020: https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf
- WSJT-X protocol specification / current user guide: https://wsjt.sourceforge.io/wsjtx-main_en.html
- Current JS8Call user guide technical implementation: https://js8call.com/JS8Call-improved/d6/d14/md_docs_2user__guide_2JS8Call__User__Guide.html
- JS8Call source, protocol research only, GPL-3.0: https://github.com/js8call/js8call
