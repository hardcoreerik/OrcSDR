# JS8 decoder status

## 2026-10-08 — slice 1

Branch target: `claude/ft8-native-bind`.

Implemented in the standalone `orcsdr::js8` module:
- documented submode profiles;
- Normal 79-tone frame geometry;
- independently evidenced Normal sync pattern from the public JS8Call API tone vector;
- raw-tone validation and extraction of the 58 data tones;
- optimized and ASan/UBSan host unit tests.

Current decode capability: **no user-visible JS8 messages yet**.

Blocked pending owner/reference captures and reconstruction:
- tone-to-bit mapping;
- exact FEC;
- exact CRC;
- upper-layer bit packing;
- SNR calibration.

Fast, JS8 40 and Slow timing/bandwidth are recorded from public documentation, but their exact sync patterns remain disabled until independently measured. JS8 60 remains experimental.

No firmware binding, UI, transmit path, PTT, CAT, scheduler or flashing work was added.


## Reconstruction tool ready

The host-only `js8-fec-reconstruct` tool is now ready for the PC capture corpus. It does not contain an external FEC matrix. It derives the GF(2) code-space rank and parity-check basis from supplied 174-bit observations.

Current blocker remains real/controlled JS8 frames plus an independently established tone-to-bit mapping.


## Tone-label search ready

The host reconstruction lab can now:
1. accept verified Normal 79-tone frames;
2. extract the 58 data tones;
3. enumerate all 40,320 tone-label permutations;
4. rank the resulting 174-bit codeword corpus;
5. pass promising mappings into the GF(2) parity-basis derivation tool.

No tone mapping has been claimed yet. The capture corpus is the next evidence dependency.


## Candidate-local PCM demodulator

The standalone module now includes a heap-free Normal-mode candidate demodulator. Given 12 kHz PCM plus a caller-supplied frame start and tone-0 audio frequency, it measures all eight tone energies per symbol, returns the 79 raw tone decisions, and reports sync hits, sync contrast and winner margin.

A deterministic host-only waveform fixture based on the public API tone vector recovers all 79 tones with added noise and rejects a deliberately wrong base-frequency candidate. This validates the receive primitive only; it is not a claim that the synthetic waveform models every JS8 modulation detail.

There is still no whole-passband candidate search, FEC, CRC, message parser, SNR calibration or firmware binding.


## Bounded Normal sync search

The standalone JS8 module now has a heap-free spectral-grid candidate search for Normal mode. It scores the three independently established sync blocks, applies bounded non-maximum suppression, and returns caller-owned candidates ranked by sync contrast.

Synthetic energy-grid tests recover the injected candidate exactly, reject a uniform-noise grid at the test threshold, and refuse Fast because its exact sync permutations have not been independently established.

This is still pre-FEC: a sync candidate is evidence of a JS8-like frame, not a decoded message.


## Exact-correlation spectral oracle

The standalone JS8 module now includes `js8_spectral.*`, an allocation-free,
float-only reference front end that converts caller-owned 12 kHz PCM into a
caller-owned non-negative energy grid.

A sandbox test starts from noisy synthetic PCM built from the documented
Normal-mode 79-tone vector and recovers the injected sync candidate at the
exact expected time row and tone-0 frequency bin. A quiet control produces no
candidate at the test threshold. Optimized and ASan/UBSan versions of this new
suite pass in the sandbox.

This implementation is deliberately a correctness oracle. A full 200-3000 Hz
exact-correlation grid is too expensive to declare as the ESP32-P4 production
strategy. The later P4 backend should use the existing optimized FFT primitive
or another measured equivalent while remaining bit/decision-compatible with
this oracle.


## WAV-to-tone extraction tool

`tools/js8-wav-tones.cpp` now connects the capture corpus to the reconstruction
lab. It accepts strict 12 kHz mono 16-bit PCM WAV input, performs the standalone
JS8 Normal spectral search, re-demodulates each candidate, and writes:

- candidate metadata as `#` comment lines;
- one exact 79-digit tone sequence per accepted raw frame.

Because `js8-tone-map` ignores comment lines, the extractor output can be
redirected directly into the tone-label search.

Sandbox fixture result:
- synthetic start: 0.160 s;
- synthetic tone-0 frequency: 900.000 Hz;
- recovered candidates: 1;
- recovered sync: 21/21;
- emitted 79-tone line exactly matched the documented API frame vector.

No real-RF interoperability claim is made from this synthetic run.


## Tone frames to 174-bit codewords

The reconstruction toolchain now includes `js8-map-codewords`. Given a
79-tone frame file and an explicitly selected eight-tone label mapping, it
strips the verified Normal sync blocks and emits one 174-bit channel word per
frame.

The label mapping is never silently assumed: it is supplied on the command
line and validated as a permutation of 0..7. This keeps the derivation step
separate from the code-space solver.

Sandbox chain validation:
`js8-wav-tones` -> `js8-map-codewords` produced one 174-bit word from the
documented Normal fixture. No claim is made that the identity mapping used in
that plumbing check is the real JS8 mapping.


## CRC-12 reconstruction lab ready

The host reconstruction suite now includes `js8-crc-reconstruct`. Once FEC
recovery yields 87-bit information words, the tool treats the first 75 bits as
payload and the trailing 12 bits as the observed check field, then searches all
12-bit feedback masks in both shift directions and both observed CRC bit
orientations.

For each hypothesis, the fixed-length affine offset is inferred from the first
word and must remain identical for every other word. This lets the reconstruction
validate an equivalent fixed-length CRC checker without guessing init/xor-out
constants.

Sandbox proof-of-tooling used a synthetic corpus generated with hidden test
parameters. The tool recovered the intended MSB representation and its
mathematically equivalent reflected form. That ambiguity is documented rather
than hidden; real JS8 bit-order evidence must select the canonical form.

No JS8 CRC parameters are claimed yet.


## Pre-FEC receiver front end complete

The standalone module now has `js8_frontend.*`, a bounded allocation-free
orchestration layer that consumes caller-owned PCM plus a caller-owned spectral
grid and performs:

```
energy grid -> bounded Normal sync search -> candidate time/frequency
            -> candidate-local PCM demod -> raw 79-tone frame
```

The output is `RawCandidate`, not `orcsdr::ft8::Decode`. It carries the raw
frame, candidate geometry and demod confidence only. FEC + CRC remain mandatory
before any user-visible decode can exist.

Sandbox end-to-end front-half test recovers the documented 79-tone Normal frame,
its exact synthetic start sample, tone-0 frequency and all 21 sync tones.
Optimized and ASan/UBSan variants pass.

At this point the receive front half and reconstruction laboratory are ready for
the PC capture corpus. The next missing decoder stage is no longer plumbing:
it is the independently reconstructed tone mapping/FEC/CRC evidence.


## Sparse parity-check recovery ready

The reconstruction lab now includes `js8-fec-sparse-search`. The existing
GF(2) solver identifies the complete parity space, but a row-reduced basis can
be dense and unsuitable for a small LDPC decoder. The new tool converts the
observed codeword basis into column signatures and searches for independent
weight-6 parity relations by matching equal three-column XOR signatures.

Every discovered check is re-validated against every supplied codeword, and
the tool reports how much of the full parity-space dimension the discovered
sparse checks actually span. If the span is incomplete it says so explicitly;
higher-weight search is then required rather than inventing missing checks.

Sandbox validation used 120 synthetic 174-bit observations with one hidden
six-variable parity relation. The tool recovered exactly the hidden
`0,1,2,3,4,5` relation, validated it on all 120 observations, and correctly
reported that one check did not span the synthetic corpus's full parity space.

This tool does not assume that real JS8 parity checks have weight six. Weight
six is simply the first bounded sparse search implemented; the real corpus
will determine whether it is sufficient.


## Graph-driven FEC decoder algorithm ready

The standalone module now contains `js8_fec.*`, a graph-driven normalized
min-sum decoder that does not embed or assume a JS8 parity graph. The graph is
supplied explicitly as check-major and variable-major adjacency arrays, and
the decoder workspace is carved from caller-owned memory.

Synthetic tests prove:
- clean valid words exit immediately;
- a weak wrong hard decision can be corrected on a small repetition-chain graph;
- invalid graphs/configuration/NaN input and undersized workspace are rejected;
- parity convergence is exposed only as FEC convergence.

The real JS8 sparse graph is still absent by design. It will be generated only
from independently reconstructed parity checks and validated on held-out
frames.
