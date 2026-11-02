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
