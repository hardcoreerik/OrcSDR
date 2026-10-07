# FT8 Decoder Research Changelog

This file tracks changes made specifically by the native OrcSDR FT8 decoder workstream. The main branch did not contain a repository-wide CHANGELOG.md when this workstream started.

## 2026-10-07 — Phase 0 research

- Created the native FT8 decoder research branch from current OrcSDR main.
- Added DECODER_RESEARCH.md covering the current FT8 protocol, existing decoder strategies, embedded constraints, ESP32-P4/Tab5 capabilities, clean-room provenance, proposed OrcSDR architecture, risks, and benchmark plan.
- Recorded that the DecoderBackend and Decode UI seam is currently on the separate FT8 UI sandbox branch rather than main; no UI work was copied into this branch.
- Established a Phase 0 stop gate: no decoder implementation begins until the owner approves the research decisions.
- No firmware/runtime code, transmit code, DSP changes, or hardware-validation claims were added.
