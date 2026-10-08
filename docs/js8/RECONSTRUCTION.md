# JS8 independent reconstruction notebook

## Objective

Produce a receive-only JS8 implementation whose protocol decisions are reproducible from public documentation and recorded evidence. The important deliverable is not only working code; it is a record showing how OrcSDR independently reconstructed and verified interoperability.

This is an engineering provenance process, not a claim of formally isolated legal clean-room development. Earlier exploratory research in the project looked at external implementations. Those source-derived observations are not implementation inputs for this reconstruction. Each implemented wire fact must have evidence recorded in `PROTOCOL_FACTS.md`.

## Evidence pipeline

One immutable owner capture should support all later checks:

```
RTL-SDR V4 + MLA-30+
        |
        +--> raw IQ + SHA-256
                |
                +--> released JS8 reference program (black-box result)
                |
                +--> deterministic 12 kHz receive WAV
                           |
                           +--> OrcSDR host reconstruction tools
                           |
                           +--> OrcSDR host decoder
                           |
                           +--> later Tab5 replay
```

## Reconstruction stages

### R0 — framing

Use public documentation and tone vectors to establish:
- 79 channel symbols;
- sync locations;
- tone count;
- submode symbol timing and spacing.

Current result: Normal framing is independently usable for raw-tone fixtures.

### R1 — tone labeling

For high-SNR known-message captures:
1. measure the winning tone for all 58 data symbols;
2. enumerate plausible three-bit tone labelings;
3. retain only mappings that produce one stable linear code across many different frames;
4. reserve recordings that were not used during selection.

No mapping is accepted because it resembles FT8.

### R2 — FEC reconstruction

Collect distinct 174-bit candidate codewords after R1. Perform GF(2) rank analysis and derive the orthogonal parity-check space.

Acceptance target:
- observed code length and rank recorded;
- enough independent frames to identify the code dimension;
- every derivation frame satisfies the resulting parity equations;
- every withheld valid frame satisfies them;
- random controls do not.

The resulting matrix/graph is generated from reconstruction output and checked into OrcSDR together with its evidence hash.

### R3 — CRC reconstruction

After reliable information-word recovery:
- collect known payload / check-bit pairs;
- solve/test polynomial, initialization, reflection, augmentation and XOR-out conventions;
- require a unique parameterization across the derivation corpus;
- validate against withheld frames.

### R4 — frame packing

Use reference-paired controlled messages to recover heartbeat, directed, compound and data framing. Each field boundary gets a named test vector.

### R5 — complete receive decoder

Only after R1-R4:
12 kHz PCM -> candidate -> tones -> bits -> FEC -> CRC -> frame parse -> plausible receive result.

## Validation sets

Keep three disjoint sets:
- **derivation**: allowed to influence reconstruction;
- **verification**: never used to select parameters;
- **noise/control**: no valid JS8 frame expected.

Every report identifies the fixture hashes assigned to each set.

## Current blocker

The exact tone labeling, FEC and CRC have not yet been independently reconstructed. The current module intentionally stops at the 58 extracted Normal data tones and cannot output a decoded message.


## Reconstruction tooling checkpoint

`tools/js8-fec-reconstruct.cpp` now accepts one observed 174-bit channel word per line and computes:
- GF(2) rank of the observed codeword space;
- nullity;
- an orthogonal parity-check basis;
- parity validation for every supplied observation.

`--emit-h` prints the independently derived parity basis as 174-bit rows.

The implementation is tested against a known four-dimensional synthetic subspace embedded in the 174-bit container. This validates the linear-algebra machinery only; it does **not** claim that any JS8 FEC definition has been recovered yet.

For real reconstruction, tone-to-bit mapping must be established first. Only then may captured codewords be fed to this tool. The intended acceptance point is rank 87 with a stable 87-dimensional orthogonal complement plus held-out-frame validation; that expected rank is a reconstruction hypothesis until measurements establish it.


## Tone-label search checkpoint

`tools/js8-tone-map.cpp` accepts verified Normal-mode 79-tone frames, strips the independently established sync blocks, and evaluates all 8! tone-to-three-bit label permutations.

For each permutation it converts the 58 data tones into a 174-bit observation and measures the GF(2) rank of the resulting corpus. The tool reports the lowest-rank mappings; it does not hard-code an expected JS8 mapping.

Why more than 87 frames matter: with 87 or fewer observations, even a wrong mapping can have rank no greater than the number of rows. The tool warns in that case. A useful reconstruction corpus should exceed the suspected information dimension and contain diverse frames, with a separate withheld set for verification.

This tool is a hypothesis filter, not proof by itself. A selected mapping must also produce a stable parity basis, validate withheld frames, and lead to a consistent CRC/message interpretation.


## Capture-to-reconstruction command path

Once a 12 kHz owner/reference WAV exists, the host path is:

```bash
js8-wav-tones capture.wav > frames.txt
js8-tone-map frames.txt
```

`frames.txt` is intentionally both human-readable and machine-readable:
metadata starts with `#`, and verified candidate frames are plain 79-digit
tone strings.

For faster investigation when a reference decoder already gives the approximate
audio frequency, narrow the extractor's frequency span:

```bash
js8-wav-tones capture.wav 1200 1800 0.45 > frames.txt
```

The current extractor uses the exact-correlation spectral oracle, so a narrow
span is preferred during reconstruction work. Full-passband performance from
this tool is not an ESP32-P4 timing estimate.


## Tone mapping to codeword export

After `js8-tone-map` identifies one or more plausible mappings, a candidate
mapping can be materialized into channel words without editing any data:

```bash
js8-map-codewords frames.txt 0,1,2,3,4,5,6,7 > codewords.txt
js8-fec-reconstruct codewords.txt --emit-h
```

The mapping string is tone-index -> three-bit label value. The example above
is deliberately only an example; it is not a JS8 protocol claim.

This separation matters for provenance: the selected mapping, the exact input
frame hashes, and the resulting 174-bit observations can all be recorded and
replayed independently.


## CRC reconstruction method

After the FEC code is reconstructed and decoded 87-bit information words are
available, feed one word per line to:

```bash
js8-crc-reconstruct info87.txt
```

For fixed 75-bit payload length, a non-zero initial state and xor-out collapse
into a fixed affine offset for any chosen polynomial/orientation. The tool
therefore searches:

- all non-zero 12-bit feedback masks;
- MSB-first and LSB-first recurrences;
- direct and reversed observed CRC-bit order;

and infers the fixed offset from the first observation. Every additional word
must reproduce that same offset.

A polynomial and its reflected representation can describe the same code under
opposite bit orientation. The tool deliberately prints such equivalent
solutions. Canonical JS8 parameters are accepted only after independent
bit-order evidence resolves that representation, and then must validate on
held-out information words.

Synthetic tool validation used a hidden 0x80F test polynomial plus a non-zero
fixed offset; the search recovered the intended representation and its
reflected equivalent. This is validation of the reconstruction method only,
not a JS8 protocol result.
