# Radio audio kept out of the repository

Decoded call audio from real over-the-air transmissions (and the reference pack that bundles it) is **not** committed to this public repository. The originals are
kept locally with their SHA-256 sums, so any result here can still be checked against them. Removed from history before the first push of this branch:

| SHA-256 | Former path |
|---|---|
| `5575b836ce89ec0e8d561209870c74a414b4963399576e0eb53d213c6b42adb3` | `artifacts/digital-voice-integration/vocoder_reference_pack.zip` |
| `88f6970e7cba8a6944e103cdecdd05b948efa412e44d2f49991bf04e54a7dd42` | `artifacts/digital-voice-integration/vocoder_reference_pack/audio/20261123_222931_22959_DMR_2C0D_CC_13_GROUP_TGT_6011885_SRC_4106019.wav` |
| `7e935d5f88436b2251c1ca6ad46bd6005187f87f33379ce904274c20107eb0cd` | `artifacts/digital-voice-integration/vocoder_reference_pack/audio/20261123_222931_29825_DMR_2C0D_CC_13_GROUP_TGT_6011885_SRC_4106011.wav` |
| `19a870a8749e8bcaf01efb58adde68be03faeb6148c3ba1fde71e3840930c500` | `artifacts/digital-voice-integration/vocoder_reference_pack/audio/20261123_222933_33523_DMR_CC_8_GROUP_TGT_6011885_SRC_4106008.wav` |
| `9dcf7ed8a25b5d0293488791f1dd1f3351b7a0eee81eed9ad20f6a68c3d1b26e` | `artifacts/digital-voice-integration/vocoder_reference_pack/audio/20261123_223004_50399_DMR_2C08_CC_8__TGT_6011885_SRC_4106019.wav` |
| `7e935d5f88436b2251c1ca6ad46bd6005187f87f33379ce904274c20107eb0cd` | `artifacts/digital-voice-integration/voice_call1_unit4106011_dsdfme.wav` |
| `9dcf7ed8a25b5d0293488791f1dd1f3351b7a0eee81eed9ad20f6a68c3d1b26e` | `artifacts/digital-voice-integration/voice_call2_unit4106019_dsdfme.wav` |

Local copies: `F:\Ai\OrcSDR-data\digital-voice\` (`vocoder_reference_pack.zip`, `voice_dsdfme\`, and `native_voice_2026_11_24\` for the native WAVs, their `SHA256SUMS.txt` and the impairment runs).
The text parts of the reference pack (`vocoder_reference_pack/README.md`, `frame_logs/`) stay in the repository; its `audio/` folder is empty on purpose.
`tools/dv_voice_compare.py` takes the reference zip as an argument (comparison only; the receiver and the vocoder never read it).
