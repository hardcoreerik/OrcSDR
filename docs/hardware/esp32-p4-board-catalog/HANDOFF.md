# ESP32-P4 RSA/REA manufacturer SDK audit — handoff

**Snapshot:** 2026-10-28. **Authoring context:** web-chat, GitHub connector + primary manufacturer documentation; **not** a run on NewcorePC.

## Safety and repo boundaries

- OrcSDR branch `research/esp32-p4-board-catalog-20261028` was READ-ONLY at `3694242a0748c3c89890fb223914db866ff25de0`. It was not edited, merged, committed, or flashed.
- Private `hardcoreerik/REA-Results` was READ-ONLY. No inbox PR or `rsa publish` occurred.
- RSA-Sandbox actual Windows path `F:\AI\RSA-Sandbox` was unavailable in this container; DNS blocks `git clone` from this runtime. `idf.py` and `rea` are not installed here.
- All source values below are **manufacturer documentation / SDK declarations**, not measurements. A GitHub source inspection is not an `rsa.py import` or `rsa.py analyze` run.
- `MANUFACTURER_SDK_QUEUE.md`, `BOARD_VERIFICATION_TRACKER.md`, `EVIDENCE_PROTOCOL.md` were referenced in the OrcSDR branch README but not present at the checked branch commit; versions in this handoff are **NEW DRAFTS**, not previously approved documents.

## Files

`BOARD_CATALOG_HANDOFF.md`, `MANUFACTURER_SDK_RECIPE_BOOK.md`, `BOARD_VERIFICATION_TRACKER.md`, `PINMAP_AND_PERIPHERAL_EVIDENCE.md`, `SOURCE_CONFLICTS.md`, `UNVERIFIED_REGISTER.md`, `SDK_RUN_LOG.md`, `ESP32P4_CATALOG_HANDOFF.json`, `HANDOFF.md`, `MANUFACTURER_SDK_QUEUE.md`, `EVIDENCE_PROTOCOL.md` and `REA_INBOX_DRAFT.md`.

## Priority P0 gaps

1. USB Host VBUS electrical supply, switches and maximum *sustained* dongle current on each exact board/revision.
2. Actual ESP-Hosted C6 coprocessor firmware versions; SDK alone does not prove it is flashed or compatible.
3. Elecrow 7 v1.2 SD card source-versus-schematic disagreement, and v1.0/v1.1 C6 D0–D3 reordering.
4. Waveshare WIFI6-DEV-KIT USB jumper selection reversed across Rev1.1 vs Rev1.2.
5. M5 Tab5 three display-controller paths: ILI9881C+GT911, ST7123, ST7121. Detect dynamically; no single panel init globally.
6. Elecrow 5 RGB display cannot be treated as DSI; do not try DSI init on it.

## Immediate next local action — exact order

```powershell
Set-Location F:\AI\RSA-Sandbox
Get-Content .\AGENTS.md
Get-Content .\README.md
python rsa.py doctor
python rsa.py list
python rsa.py import github:espressif/esp-bsp@9112bd334726368d4aa4421dd01f2e4b1a739568 --name esp-bsp-p4-catalog
python rsa.py analyze esp-bsp-p4-catalog
python rsa.py import github:espressif/esp-hosted-mcu@78b1c1c43c02b0c4e898cf87f94679278b1ddaff --name esp-hosted-mcu-p4-catalog
python rsa.py analyze esp-hosted-mcu-p4-catalog
```

Review license receipts before each subsequent import. For LilyGO, clone/import the **pinned submodule** using rsa.py separately; the root repository references driver SHA `a47cf73e2c9d442c0f00b618749150f4482dcd0a`. If `rsa.py` does not support submodule materialization, use its sanctioned extension/test path rather than bypassing import accounting.

Only after analysis, commit notes **inside RSA-Sandbox**, run `python rsa.py publish --dry-run`, review secret scan and diff, and use the authorized `rsa.py publish` path. Do **not** hand-edit generated REA-Results indexes. Do not merge any branch without user approval.

## Scoreboard (this web-session only)

- Catalog entries: **40** candidate revision/assembly records (NOT 40 verified commercial products).
- Source repository commits pinned/read: **9** (includes two unverified-license Elecrow repos; M5GFX/M5Unified license check still due).
- Unique products with some SDK/wiki evidence: **20**.
- Entries with selected source GPIO extracts: **6**. This is NOT a complete pinmap signoff.
- rsa.py imports/analyses completed **this session**: **0/0**.
- IDF builds completed **this session**: **0**.
- Physical runtime tests completed **this session**: **0**.
- Fully validated production profiles **this session**: **0**.
- Known documented conflict gates: **4 core P0/P1 groups**, detailed in `SOURCE_CONFLICTS.md`; additional unknown fields remain.

## Evidence provenance

OrcSDR branch: https://github.com/hardcoreerik/OrcSDR/tree/research/esp32-p4-board-catalog-20261028

REA prior work: https://github.com/hardcoreerik/REA-Results/blob/main/targets/esp32-p4-devkit/notes/pcb-lessons-2026-10-16.md (October 16, 2026, pinned Olimex commit in the original report); REA index snapshot from RSA Sandbox commit `f28e3b0af028211e70d910fb2deb3876915f8833` dated October 25, 2026.

Generated handoff file checksums are in `HANDOFF_CHECKSUMS.sha256`; these hash only these documents, not remote manufacturer files.