# Digital Voice — REA research IQ sample handoff

> **Development data is available now.** Our private REA-Results research branch contains **two actual binary, real-air DMR IQ files**. This is not merely a competitor list or proposed dataset. They can be used for repeatable host-side OrcSDR decoder development and regression tests. The test data must **NOT be merged** into the OrcSDR firmware or REA main branches.

## Verified location

- **Research repository:** [hardcoreerik/REA-Results](https://github.com/hardcoreerik/REA-Results) (private).
- **Research branch:** `chatgpt/digital-voice-rsa-rea-2026-11-01` (separate and unmerged).
- **Exact stored-samples commit:** [bf3ac365f0f2e2c06527bae36c6a0da5754fe8f2](https://github.com/hardcoreerik/REA-Results/commit/bf3ac365f0f2e2c06527bae36c6a0da5754fe8f2).
- **Directory:** [inbox/fixtures/digital-voice](https://github.com/hardcoreerik/REA-Results/tree/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice).
- **Readme and metadata:** [README.md](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/README.md) and [manifest.json](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/manifest.json).

### Actual files available for development

| REA file | Protocol | Bytes | Format / rate | Original Git blob SHA-1 |
|---|---|---:|---|---|
| [DMR/dmr-t3-cc.cfile](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/DMR/dmr-t3-cc.cfile) | DMR Tier III control | **499,200** | CF32 interleaved I/Q, 48 kS/s | `7a119ec16e5c6412488f2ed112b2c5b85cdd799f` |
| [DMR/dmr-voice-term.cfile](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/fixtures/digital-voice/DMR/dmr-voice-term.cfile) | DMR voice terminator | **537,600** | CF32 interleaved I/Q, 48 kS/s | `91620e07e5350af6bce85d75152c23c62bfed253` |

Combined: **2 real-air recordings; 1,036,800 bytes; 2.70 seconds**. These are independently verified source-to-destination Git object matches; actual local SHA-256 and OrcSDR test passes are still pending.

Original source: GopherTrunk at immutable commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`. Upstream tests: [dmr_realcapture_test.go](https://github.com/MattCheramie/GopherTrunk/blob/f06bfff493f47fdeb69807c8b08b2b32a253839f/cmd/gophertrunk/dmr_realcapture_test.go). Sample capture is **narrowband channelized IQ**, not the original 2-MS/s wideband RF capture and **not CU8**.

### Reference test assertions (upstream, not OrcSDR verified)

**Control file:** >= 1 control lock; >= 12 CRC-valid CSBKs; >= 8 Aloha beacons. **Voice signaling file:** >= 8 RS-validated Full-LC terminators identifying talkgroup **24**, source **4209000**. The second fixture is from Tier III and does not establish conventional Tier II call-header or audio behavior by itself.

## How to retrieve and test

1. Do not merge any branch. Clone/fetch the **REA-Results** repository separately, switch to `chatgpt/digital-voice-rsa-rea-2026-11-01`, and navigate to `inbox/fixtures/digital-voice/DMR/`.
2. Verify `Get-Item .\inbox\fixtures\digital-voice\DMR\*.cfile | Select-Object Name,Length` and `git hash-object` of each file against the blob IDs above.
3. Obtain local SHA-256 with `Get-FileHash <path> -Algorithm SHA256`; record hashes and input lengths in any future RSA-generated metadata after actual import.
4. In `F:\AI\RSA-Sandbox`, run `python rsa.py doctor`; follow the sandbox AGENTS.md contract and use rsa.py, not custom manually written substitute metrics, for import/analyze/compare.
5. Build a deterministic host-side OrcSDR **48-kS/s CF32** IQ replay adapter or use an existing verified one after inspecting actual code; preserve the original complex sample order, center offset and any discontinuity metadata.
6. For each file, report source blob ID, local SHA-256, input format, symbol-lock time, accepted FEC/CRC counts, talkgroup / radio IDs, error counts, raw decoded frame logs and final pass/fail. Save logs and replay artifacts outside this repository.
7. After host acceptance, test any ESP32-P4 implementation independently, reporting CPU, stack, heap, task time, IQ drops and audio continuity. Historical P25 Phase I regression tests must remain passing.

## Other samples — links available, binaries NOT here

The [detailed REA research IQ matrix](https://github.com/hardcoreerik/REA-Results/blob/chatgpt/digital-voice-rsa-rea-2026-11-01/inbox/2026-11-01_digital-voice-iq-capture-verification-matrix.md) tracks NXDN48/96 IQ WAV objects, Signal Identification Wiki DMR/NXDN/P25 IQ candidates, and a P25 Phase I+II **post-demodulation dibit** capture with known reference SHA-256 and counts. They have NOT been downloaded into this private REA branch; the currently available binary-copy tools could not transfer the large/source-attachment files. The Phase II sample is **not RF IQ**. Do not mark the full corpus as acquired.

## No merges, firmware code or public redistribution

This documentation-only file marks where fixtures are stored, as requested. Neither the REA research files nor third-party audio/IQ binaries should be merged into production, or distributed beyond appropriately licensed private research, without explicit approval and rights review. The same rules apply to future source downloads. No decoder test is asserted to have passed merely by creating this handoff.
