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
