# NXDN receive decoder — engineering evidence index

The implementation lives in `components/orcsdr_nxdn/`. All work targets `feature/dmr-digital-voice-foundation`. This is first-party independent receive-only host code. It is not yet a live Tab5 Digital Voice scanner.

Read `ARCHITECTURE.md`, `DECODER_STATUS.md`, `REAL_IQ_VALIDATION.md`, and `NEXT_STEPS.md`. Follow the host build and replay commands in `components/orcsdr_nxdn/README.md`.

The private REA branch `research/nxdn48-nxdn96-foundation-20261119` already contains three real-air fixtures with source-pinned blob identities and deterministic noise/AM controls. No need to reacquire or republish them in public OrcSDR. Real-air reference assertions belong to the upstream project until actually reproduced with this decoder.

Latest assessment (replay results, standards gap, voice-branch audit): `RESEARCH_ASSESSMENT_2026-11-25.md`.
