# Digital Voice — REA research IQ sample handoff

> **Development data is available now.** Our private REA-Results research branch contains **six actual binary, real-air DMR and P25 Phase I IQ files**. This is not merely a competitor list or proposed dataset. They can be used for repeatable host-side OrcSDR decoder development and regression tests. The test data must **NOT be merged** into the OrcSDR firmware or REA main branches.

## Verified location

- **Research repository:** [hardcoreerik/REA-Results](https://github.com/hardcoreerik/REA-Results) (private).
- **Research branch:** `chatgpt/digital-voice-rsa-rea-2026-11-01` (separate and unmerged).
- **Exact stored-samples commit:** [2434efbab254d1e3051ca62f68bfb22648472ba3](https://github.com/hardcoreerik/REA-Results/commit/2434efbab254d1e3051ca62f68bfb22648472ba3).
- **Directory:** [inbox/fixtures/digital-voice](https://github.com/hardcoreerik/REA-Results/tree/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice).
- **Readme and metadata:** [README.md](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/README.md) and [manifest.json](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/manifest.json).

### Actual files available for development (four DMR, two P25 Phase I)

| REA file | Protocol | Bytes | Format / rate | Original Git blob SHA-1 |
|---|---|---:|---|---|
| [DMR/dmr-t3-cc.cfile](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/DMR/dmr-t3-cc.cfile) | DMR Tier III control | **499,200** | CF32 interleaved I/Q, 48 kS/s | `7a119ec16e5c6412488f2ed112b2c5b85cdd799f` |
| [DMR/dmr-voice-term.cfile](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/DMR/dmr-voice-term.cfile) | DMR voice terminator | **537,600** | CF32 interleaved I/Q, 48 kS/s | `91620e07e5350af6bce85d75152c23c62bfed253` |
| [DMR/dmr-directmode-446500-keyup-48k.cs16](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/DMR/dmr-directmode-446500-keyup-48k.cs16) | DMR 446.500 MHz direct-mode keyup | **345,600** | CS16 LE I/Q, 48 kS/s | `e305eac29985ff647b8aba0434fee8ab7db81f15` |
| [DMR/dmr-directmode-446500-ptt2-48k.cs16](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/DMR/dmr-directmode-446500-ptt2-48k.cs16) | DMR second PTT acquisition | **326,404** | CS16 LE I/Q, 48 kS/s | `b7d91747030e55f68637edb2198476047191d84a` |
| [P25/mmr-city-cc.cfile](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/P25/mmr-city-cc.cfile) | P25 Phase I City control | **576,000** | CF32 LE I/Q, 48 kS/s | `94782fe613a4c34311dfdd7dba1be0e6db66a228` |
| [P25/mmr-s9-cc.cfile](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/P25/mmr-s9-cc.cfile) | P25 Phase I Site 9 control | **419,432** | CF32 LE I/Q, 48 kS/s | `cd803917bcde5f3db397772551ba6d75b1490487` |

Combined: **6 real-air recordings; 2,704,236 bytes; approximately 8.79 seconds**. These are independently verified source-to-destination Git object matches; actual local SHA-256 and OrcSDR test passes are still pending.

Original source: GopherTrunk at immutable commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`. Upstream tests: [dmr_realcapture_test.go](https://github.com/MattCheramie/GopherTrunk/blob/f06bfff493f47fdeb69807c8b08b2b32a253839f/cmd/gophertrunk/dmr_realcapture_test.go). Sample capture is **narrowband channelized IQ**, not the original 2-MS/s wideband RF capture and **not CU8**.

### Reference test assertions (upstream, not OrcSDR verified)

**DMR Tier III control:** >= 1 control lock, >= 12 CRC-valid CSBKs, >= 8 Aloha beacons. **DMR Terminator LC:** >= 8 RS-validated terminators, talkgroup **24**, source **4209000**. **DMR direct mode:** >= 8 mobile data syncs, >= 2 voice syncs, exactly one grant identifying talkgroup **99** / radio source **3024109** / color code **1**, and >= 2 voice superframes. **P25 MMR City:** >= 12 trusted NIDs, >= 40 decoded TSBKs, zero TSBK CRC failures, NAC **0x164**. **P25 MMR Site 9:** >= 8 trusted NIDs, >= 24 TSBKs, plausible lock NAC in **0x160–0x167** (nominal **0x167**). All are upstream thresholds, not measured OrcSDR results; original P25 tests: [replay_realcapture_test.go](https://github.com/MattCheramie/GopherTrunk/blob/f06bfff493f47fdeb69807c8b08b2b32a253839f/cmd/gophertrunk/replay_realcapture_test.go).

## How to retrieve and test

1. Do not merge any branch. Clone/fetch the **REA-Results** repository separately, switch to `chatgpt/digital-voice-rsa-rea-2026-11-01`, and navigate to `inbox/fixtures/digital-voice/DMR/`.
2. Verify `Get-Item .\inbox\fixtures\digital-voice\DMR\*.cfile | Select-Object Name,Length` and `git hash-object` of each file against the blob IDs above.
3. Obtain local SHA-256 with `Get-FileHash <path> -Algorithm SHA256`; record hashes and input lengths in any future RSA-generated metadata after actual import.
4. In `F:\AI\RSA-Sandbox`, run `python rsa.py doctor`; follow the sandbox AGENTS.md contract and use rsa.py, not custom manually written substitute metrics, for import/analyze/compare.
5. Build a deterministic host-side OrcSDR **48-kS/s CF32 or CS16 (as specified per fixture)** IQ replay adapter or use an existing verified one after inspecting actual code; preserve the original complex sample order, center offset and any discontinuity metadata.
6. For each file, report source blob ID, local SHA-256, input format, symbol-lock time, accepted FEC/CRC counts, talkgroup / radio IDs, error counts, raw decoded frame logs and final pass/fail. Save logs and replay artifacts outside this repository.
7. After host acceptance, test any ESP32-P4 implementation independently, reporting CPU, stack, heap, task time, IQ drops and audio continuity. Historical P25 Phase I regression tests must remain passing.

## Other samples — links available, binaries NOT here

The [detailed REA research IQ matrix](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/2026-11-01_digital-voice-iq-capture-verification-matrix.md) tracks NXDN48/96 IQ WAV objects, Signal Identification Wiki DMR/NXDN/P25 IQ candidates, and a P25 Phase I+II **post-demodulation dibit** capture with known reference SHA-256 and counts. They have NOT been downloaded into this private REA branch; the currently available binary-copy tools could not transfer the large/source-attachment files. The Phase II sample is **not RF IQ**. Do not mark the full corpus as acquired.

## No merges, firmware code or public redistribution

This documentation-only file marks where fixtures are stored, as requested. Neither the REA research files nor third-party audio/IQ binaries should be merged into production, or distributed beyond appropriately licensed private research, without explicit approval and rights review. The same rules apply to future source downloads. No decoder test is asserted to have passed merely by creating this handoff.

## Before decoder implementation: updated RSA/REA evidence gaps (Nov 2, 2026)

**Do not mistake stored IQ for complete protocol validation.** The [REA-Results predevelopment readiness audit](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/2026-11-02_digital-voice-predevelopment-data-gap-audit.md) identifies missing materials, priority, quantified fixture coverage, and GO/NO-GO conditions.

**Ready now:** shared receiver/tuner event architecture, UI layout, replay harness, source-pinned DMR/P25 I regression experiments. **Do not claim supported until tested:** clear DMR/NXDN voice, P25 II voice, automatic protocol classification/trunk following. Main missing evidence includes: NXDN48 and NXDN96 verified real-air clear IQ with reference FEC/IDs; a P25 II symbol-vector import with checked extracted SHA-256 and independent output; P25 II **actual OTA IQ** with matching control/slot context; P25 Phase I CQPSK and clear voice; full DMR Tier II conventional calls; labeled negatives, long recordings and a validated audio vocoder/distribution path. The six stored clips are short (1.09–1.8 s) and cannot establish long-term audio stability.

**Important manifest correction:** the manifest now records *per-file sample encodings*: DMR direct-mode is **CS16**, the other DMR and P25 files are **CF32**, all 48 kS/s. Do not use a global CF32 assumption. The associated REA research commit for the correction is `cc92c09f07f9d8491b3bb9c37784dc5ae0f59eb0`.

This remains **research documentation only**, not a request to merge results branches or copy third-party binaries into a firmware image.

## Critical-gap acquisition update — 2026-11-02

**LATEST REA SOURCE OF TRUTH:** [fixture README](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/README.md) and [machine-readable manifest](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/manifest.json). The earlier six-file table above is preserved as a dated checkpoint, not the current complete inventory.

**Available NOW on the separate private REA-Results research branch: 11 binary IQ fixtures totaling 4,048,236 bytes.**

- **Nine source-hash-verified REAL-AIR samples:** four DMR, two P25 Phase I C4FM control, and three newly acquired NXDN48/96 (two conventional/direct voice slices and one 453.775 MHz RCCH control sample). All stored under `inbox/fixtures/digital-voice/`. Source: GopherTrunk commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`.
- **Two SANDBOX-GENERATED synthetic AUTO negatives:** 48-kS/s CS16 noise and analog-AM baseband with actual Python-computed SHA-256 and verified Git SHA-1. Source/recreation script: `inbox/fixtures/digital-voice/AUTO/generate_controls.py`. These are NOT real RF and provide no decoder success evidence.
- **Newest source commits:** [NXDN real-air IQ acquisition](https://github.com/hardcoreerik/REA-Results/commit/27643bf8ba4700c516c586e96960e9249d877b6c) and [AUTO negatives](https://github.com/hardcoreerik/REA-Results/commit/10d7dae085e0a31515e0449839e7463177272a40).

### Exact NXDN binary fixtures and upstream verification goals

| Source path within REA fixtures | Size | Format | Rate | Reference test |
|---|---:|---|---|---|
| `NXDN/nxdn48_rdch_voice_447100_16oct_48k.cs16` | 230,400 B | CS16 LE IQ | 48 kS/s | >=32 voice frames; pitch continuity >=0.50; mean Golay corrections <=1/frame |
| `NXDN/nxdn96_rdch_voice_447100_15oct_48k.cs16` | 153,600 B | CS16 LE IQ | 48 kS/s | >=24 voice frames; pitch continuity >=0.50; mean Golay corrections <=1/frame |
| `NXDN/nxdn96_rcch_cc_453775_19oct_48k.cs16` | 192,000 B | CS16 LE IQ | 48 kS/s | >=18 valid FSW+LICH frames spaced 192 dibits; carrier estimate -750..-550 Hz |

**Warning:** NXDN96 RCCH source test validates sync and LICH parity but does **not** validate CAC CRC / full trunk grant decoding. The NXDN voice source uses a pitch-index heuristic; that does **not** prove intelligible AMBE+2 audio. Treat upstream thresholds as comparison targets, not firmware pass statuses.

### P25 Phase II: real IQ is not acquired; still a blocker

[REA Phase II capture and provenance research](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/2026-11-02_nxdn-realair-acquired_p25-phase2-acquisition-plan.md) contains the exact live capture commands, source ground-truth targets and artifact schema.

- Source-reported p25p1andp2.bin dibit file is available through a [DSD-FME issue #304 attachment](https://github.com/lwvmobile/dsd-fme/issues/304#issuecomment-3247474181); its **extracted data** has published SHA-256 `136bdb57d08c8ed4a9dd7b5f8a2e7476f995b60aafb45de27a0189fd66ce253b`. NOT copied into REA due unsupported GitHub issue binary download. It's **recovered dibits, NOT raw IQ**.
- GopherTrunk's [P25 Phase II groundtruth replay test](https://github.com/MattCheramie/GopherTrunk/blob/f06bfff493f47fdeb69807c8b08b2b32a253839f/internal/radio/p25/phase2/groundtruth_replay_test.go) documents a separately owned 120-second 2.048-MS/s CF32 captured signal with 16 independent SDRTrunk source-RID references, but the IQ bytes are NOT published in the repository. Do not claim we have acquired it.
- Prefer capturing a fresh confirmed clear live Phase II voice channel with OrcSDR's existing working P25 control/grant following, preserving WACN/SystemID/NAC, TGID, voice carrier and slot. The published OSRP frequencies are **historical observations**, not guaranteed active assignments today. Full commands and metadata checklist are in the research report.

### Additional data integrity warning

A secondary DSD-neo CU8 fixture transfer through the available connector **modified raw byte encodings**, making destination blob hashes differ from source hashes. Those invalid transfers were **not referenced in REA branch commits**. Do not use them, even if a decoder appears to synchronize. Use GopherTrunk's byte-verified real NXDN samples instead, and preserve Git SHA-1 plus actual local SHA-256 for every further imported file.

### How to run/test the next decoder session

1. Fetch the private REA research branch separately without merging. Check `inbox/fixtures/digital-voice/manifest.json`; **all per-file formats override any global defaults**.
2. Run `Get-ChildItem -Recurse -File -Include *.cs16,*.cfile` against the checked-out fixture directory, then calculate local SHA-256 with `Get-FileHash -Algorithm SHA256`. Verify each source `git hash-object` matches the manifest's `git_blob_sha1`.
3. Start native RSA in `F:\AI\RSA-Sandbox` and run `python rsa.py doctor`; import/analyze through `rsa.py` as required by its `AGENTS.md`. **Do not invent RSA metrics or modify generated files**.
4. In a host replay harness, explicitly select `cs16-le` or `cf32-le`, 48,000 complex samples/s; use 2400-sym/s for NXDN48 and 4800-sym/s for NXDN96. Establish baseline good frame counts before tuning filters or writing UI claims.
5. Use the two known-negative AUTO samples and verify the classifier returns UNKNOWN; count false sync hits, false CRC-accepts, false-positive events, and time-to-abstain. Synthetic controls cannot substitute for real analog/NFM/other-digital negatives.
6. Save run logs, hashes, board/driver revision and numerical counters, then validate host vs ESP32-P4 replay. P25 Phase I hardware regressions remain mandatory.
7. Keep **unverified** protocol and audio states honestly labeled on the unified dashboard. Separate `carrier detected`, `sync acquired`, `CRC validated`, `clear vocoder frames available`, and `PCM confirmed`. Never conflate grant/phase sync with live intelligible speech.
8. Do not merge the REA branch or publish third-party captured voice/audio to public releases. User approval is required for any merge.

**This handoff is documentary.** The new NXDN files and negative controls are in REA-Results, not the firmware. No decoder implementation or hardware validation was performed in this research turn.

### New P25 Phase II dibit fixture downloader (research-only, 2026-11-02)

The source-verified PowerShell helper is [REA-Results/P25/get-phase2-dibits.ps1](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/P25/get-phase2-dibits.ps1). Run it from a **separate** REA research-branch checkout on the Windows RSA workstation. It downloads the issue #304 archive, extracts only the named BIN, rejects any extracted SHA-256 not equal to the pinned LakeShark reference hash, writes a local manifest and saves a UTC-stamped `acquisition.log` in the output folder. See the REA fixture README for exact commands.

**Status**: helper committed and its own Git object SHA verified identical to the sandbox-authored script. **It has not actually downloaded the dibit archive on Windows in this session**, so the source data remains an external candidate. This helper is a reproducible acquisition gate, not decoder implementation and not true over-the-air P25 Phase II raw IQ. No third-party ZIP or BIN is committed by this helper.
