# Prompt for the external vocoder work (paste into ChatGPT)

Owner: OrcSDR project. This document is the brief for a separate, external workstream. It is self-contained. Nothing here authorises copying
code from other projects.

---

## Role and goal

You are a DSP/embedded engineer. Build a **receive-only DMR voice decoder library** ("`orcsdr_dmr_voice`") that turns raw DMR voice frames into 8 kHz mono
16-bit PCM, for a battery-powered handheld SDR (M5Stack Tab5, ESP32-P4). It will be plugged into an existing DMR receiver that already finds calls,
validates their headers and extracts voice frames. You deliver the vocoder side only.

## What the existing receiver already does (do not rebuild it)

- RTL-SDR IQ -> 4FSK demodulation -> burst/sync detection -> slot type, BPTC, CRC/RS validation, call identity (source, talkgroup, colour code, timeslot).
- Real captures show calls on a commercial Tier III system; headers are decoded and cross-checked against DSD-FME.
- It deliberately **ignores voice bursts today**. A voice-burst extractor is being added by the project (see "Interface"); you may assume it exists.

## Interface you will receive (fixed contract, you code against it)

One DMR voice burst carries three vocoder frames. The extractor hands you, per frame:

```cpp
namespace orcsdr::dmr_voice {
struct VoiceFrame {
  uint8_t bits[9];        // 72 channel bits, MSB first, in over-the-air order (before any deinterleave or FEC), as sliced from the burst
  uint8_t slot;           // 1 or 2, 0 = unknown
  uint8_t burst;          // 0..5 position in the voice superframe (A..F)
  uint8_t frame_in_burst; // 0..2
  uint8_t colour_code;
  bool    encrypted;      // privacy indicator / protected flag seen for this call
  uint32_t call_id;       // increments per call on a channel; frames of one call share it
};
}
```

- Frames arrive every 20 ms of speech (three per 60 ms burst), in order, with possible gaps (lost bursts). A gap must produce comfort-noise or
  concealment, not a crash.
- **If `encrypted` is true you must output silence and never attempt to decrypt, strip or bypass anything.** Receive-only, no transmit path, no key handling.

Your library must expose something like:

```cpp
class VoiceDecoder {
 public:
  void reset();                                          // new call or retune
  // Returns the number of PCM samples written (160 for a good 20 ms frame, 0 if the frame is not usable)
  size_t decode(const VoiceFrame& f, int16_t* pcm, size_t capacity);
  void conceal(int16_t* pcm, size_t capacity);           // a missing frame
  struct Stats { uint32_t frames, bad_fec, repaired_bits, concealed; };
  Stats stats() const;
};
}
```

## Hard constraints

1. **Clean-room, licence-safe.** The repository is AGPL-3.0-or-later and has a rule: **no GPL code, no code read-and-translated from GPL projects
   (mbelib, DSD, DSD-FME, OP25, SDRangel, dsdcc, etc.)**. Do not paste, paraphrase or port their source. Work from published specifications and
   papers only, cite every source you use, and say plainly where a specification is not public.
2. **Licence/patent honesty.** DMR voice uses an AMBE+2 family vocoder, which is proprietary (DVSI). State clearly, with sources, what is public
   (frame structure, FEC, bit allocation, whatever is published), what is not, the patent situation as far as you can verify it, and any risk that
   the project's owner needs to decide on. **Do not guess missing parameter tables.** If a clean-room decoder is not possible from public material,
   say so and propose alternatives (licensed library, DVSI hardware chip over SPI/UART, a different vocoder for a different mode) with their costs.
3. **Target:** ESP32-P4, RISC-V dual core 400 MHz, **single-precision FPU only (double is software and ~20x slower)**, internal RAM is nearly
   exhausted (budget: at most 4 KB of internal-RAM statics; everything else in PSRAM), ESP-IDF 5.5, C++17, no exceptions, no RTTI, no heap
   allocation in the decode path. Real-time: a 20 ms frame must decode in well under 5 ms on one core while the radio and screen run.
4. **No dependency direction problems.** The library must not depend on OrcSDR app code, ESP-IDF headers or the display; it builds on a plain host with
   g++/clang for tests.
5. Receive-only. No encoder, no transmit code, no key handling.

## Deliverables

1. A written **feasibility and legal-provenance report first** (1-2 pages): what is publicly specified, what is missing, patent/licence status with
   citations, and a go / no-go recommendation. Stop and report if the answer is no-go.
2. If go: the library (`include/`, `src/`), a CMake host test target, and a README describing provenance of every table and algorithm.
3. **Tests with real expectations:** known-answer tests for each stage (deinterleave, FEC correction counts on injected bit errors, parameter
   decode, synthesis) using vectors you can justify. Say how each vector was produced. If no official reference vectors exist, propose a validation
   plan that does not rely on guessing (for example comparing against recordings from radios the owner controls, or an objective speech-quality
   measure on a frame set the owner supplies).
4. Performance numbers measured or estimated honestly (cycles per frame on a host build, plus a clearly labelled estimate for the P4).
5. A short note on **what the owner must decide** and what remains unverified.

## Honest-engineering rules (the project's standard)

- Never claim something works without a test that shows it. Say "not verified" where that is the case.
- Never present a guess as a specification. Unknown stays unknown.
- Do not weaken a check to make a test pass.
- Keep the code small, readable and commented with the reason for each non-obvious step.
- Every change to the repository must be recorded in `CHANGELOG.md` (the project's CI enforces it).

## How the owner will validate

The owner has three real recorded calls (14 s each, 240 kS/s IQ) and two DMR-capable receivers for comparison. Once the extractor exists the project
will supply the extracted `VoiceFrame` streams for those calls as binary files plus the expected call metadata. Your decoder's output will be judged by
listening and by comparing speech intelligibility against the same calls decoded by an independent reference the owner has access to.
