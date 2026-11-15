# Digital Voice — consolidated receive-only branch (2026-11-15)

**Canonical branch:** `feature/dmr-digital-voice-foundation`.

**Before integration:** DMR head `aab547c3eb6b80a3766921fdecddf5f93c3ad9a4`; D-STAR/YSF branch `feature/dstar-ysf-rx-foundation-20261114` head `94daf50f77204fdedb3ee2191af01d3097bcb438` (based on `main` `96a5b8a0ee749100e2b259b21b11149f283a93ec`).

## Source preservation and merge strategy

Seven independent source commits were replayed in their original order as **cherry-pick-equivalent new commits** on the original DMR HEAD. Each new commit cites the original source SHA and retains its original Git blob, without merging unrelated `main` ancestry. The workflow trigger and README were then updated in a separate integration commit. No force push or modification to DMR decoding, existing P25, radio drivers, firmware, Pluto, or TX.

| Original commit | Contents |
|---|---|
| `a33a140` | Host CMake build |
| `2f07747` | Metadata API |
| `77acb08` | YSF FICH and D-STAR DV header parser |
| `3d9c520` | Hard-bit JSONL replay CLI |
| `19e3396` | Independent synthetic host tests |
| `c1339fb` | README and limits |
| `94daf50` | Release and ASan/UBSan workflow |

Original branch Release + sanitizer CI passed: https://github.com/hardcoreerik/OrcSDR/actions/runs/43690924207 . The consolidated branch has a retargeted workflow and `digital-voice-unified-host.yml` for both actual native C++ DMR and D-STAR/YSF host modules.

## Verified scope vs outstanding evidence

- **DMR:** Retains existing 48 kS/s IQ frontend, D0/D1 protocol, real-air metadata recovery and protocol tests. Its exact four-fixture D1-N runner executed 40/40, but its *official gate is FAIL* due to mobile-station sync time-slot assumptions. Do not claim D1-N passed until the acceptance semantics are fixed and retested.
- **D-STAR/YSF:** Receive-only metadata parsers on **already demodulated hard bits**, confirmed with independent transmit-side synthetic frame tests and protocol integrity checks. Not yet native IQ-to-bits, real-air RF acceptance, Tab5 support, or licensed AMBE audio.
- **P25, NXDN and discovery:** Existing platform features and research remain independently scoped; no P25 or NXDN source modified in consolidation.
- **Pluto and TX:** Entirely separate development work; no receiver driver or radio owner introduced.

## Future branch policy

Create task branches from `feature/dmr-digital-voice-foundation` and integrate completed, tested RX digital-voice work back into it. The former D-STAR/YSF branch remains a historical reference only. No merge to `main` without separate review/approval.
