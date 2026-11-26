# NXDN follow-up engineering gates

1. DONE 2026-11-25 for the three REA clips (see RESEARCH_ASSESSMENT_2026-11-25.md); still open: why the RCCH clip has no frames before dibit 1017, the two skipped NXDN48 frames, and the undecoded WaveShark clips. (Original item:) Run pinned real-air CLI replay on all three REA fixtures; record actual frame, offset, timing and CRC counts. Do not import upstream reported counts as OrcSDR observations. Evaluate the deterministic REA AUTO noise/AM negative IQ and independent NXDN48/96 examples.
2. Improve real-air frontend as indicated by measured failures: channel filtering, carrier-offset, polarity, timing, reacquisition and chunk equivalence; prove separate NXDN48/NXDN96 acceptance and P4 CPU/stack/heap/PSRAM budgets.
3. Independently implement validated FACCH1, Golay-protected 49-bit voice data extraction and integrity-gated source/destination L3 parsing; no speech output without lawful AMBE+2 codec rights/technical feasibility.
4. Type-C: rebuild genuine RCCH CAC full FEC/CRC and independently test grants, site/channel maps and traffic following against genuine captures.
5. Type-D: obtain genuine distributed SCCH/home-repeater tests and qualify separately; no assumption Type-C grants prove Type-D.
6. Connect evidence-qualified events to the existing scanner-first SCAN/LIVE/LAST HEARD/SYSTEMS/SIGNAL/SETUP M5GFX workspace only after receiver-owned read-only IQ adapter, retune epochs, and resource measurements. No new tuner owner, main.cpp DSP, or unsupported dashboard claims.
7. Preserve the existing DMR, D-STAR/YSF and P25 host conformance suites and run regression on latest shared branch before main-merge review.
8. Obtain an authorised copy of NXDN TS 1-A (CAI) and related parts before any Layer 3, FACCH, CAC or Golay work; see RESEARCH_ASSESSMENT_2026-11-25.md section 2.
