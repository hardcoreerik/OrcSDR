# LoRa Corpus Manifest and Differential Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Catalog every useful local ORCIQ capture with immutable identity and produce a reproducible host/native differential report before modifying the native decoder.

**Architecture:** Keep raw IQ under ignored `artifacts/lora_validation/corpus/`. Check in metadata only at `tools/lora_lab/corpus_manifest.json`. A small Python verifier validates the schema, hashes and ORCIQ headers, reruns the existing host decoder, and writes an ignored JSON report; native results remain explicitly sourced from live-device evidence until replay exists.

**Tech Stack:** Python standard library, existing `tools/decode_orciq.py`, `unittest`, JSON.

**Spec:** `docs/LORA_INDEPENDENT_VALIDATION.md`

## Global Constraints

- Never commit raw IQ, credentials, channel keys, or private key material.
- Use relative corpus paths and immutable capture IDs.
- Preserve `controlled-positive`, `uncontrolled-background-lora`, `confirmed-non-lora-negative`, and `unknown` as distinct classifications.
- Preserve host and native outcomes separately; missing native evidence is `unknown`, not failure.
- Do not change production/native decoder behavior in this plan.

---

### Task 1: Manifest schema and validation

**Files:**
- Create: `tools/lora_lab/corpus_manifest.py`
- Create: `tools/lora_lab/test_corpus_manifest.py`
- Create: `tools/lora_lab/corpus_manifest.json`

**Interfaces:**
- Consumes: manifest JSON with top-level `schema_version` and `captures`.
- Produces: `load_manifest(path: Path) -> dict` and `validate_manifest(manifest: dict) -> None`.

- [x] **Step 1: Write the failing schema test**

```python
def test_manifest_rejects_duplicate_capture_ids(self):
    capture_id = "orciq-aaaaaaaaaaaaaaaa"
    manifest = {"schema_version": 1, "captures": [self.entry(capture_id), self.entry(capture_id)]}
    with self.assertRaisesRegex(ValueError, "duplicate capture_id"):
        validate_manifest(manifest)
```

- [x] **Step 2: Run the focused test and verify it fails because the module is absent**

Run: `python -m unittest test_corpus_manifest -v` from `tools/lora_lab`.

- [x] **Step 3: Implement the minimum validator**

Validate required fields, unique IDs, lowercase 64-character SHA-256 values, relative `.orciq` paths, allowed classifications, and differential classes `A`, `B`, `C`, `D`, or `unknown`.

- [x] **Step 4: Run the focused test and all LoRa lab tests**

Run: `python -m unittest discover -s tools/lora_lab -p "test_*.py" -v`.

### Task 2: Local corpus verification and host replay

**Files:**
- Modify: `tools/lora_lab/corpus_manifest.py`
- Modify: `tools/lora_lab/test_corpus_manifest.py`

**Interfaces:**
- Consumes: `tools/lora_lab/corpus_manifest.json` and local `artifacts/lora_validation/corpus/` files.
- Produces: `verify_capture(entry: dict, corpus_root: Path) -> dict` and CLI JSON output.

- [x] **Step 1: Write a failing real-file test**

Create a temporary ORCIQ file with the existing `decode_orciq.HEADER`; assert that `verify_capture` reports a hash mismatch after one payload byte changes.

- [x] **Step 2: Run the focused test and verify the expected failure**

Run: `python -m unittest test_corpus_manifest.CorpusManifestTests.test_verify_capture_detects_hash_mismatch -v` from `tools/lora_lab`.

- [x] **Step 3: Implement verification and replay**

Check SHA-256 and header metadata before calling `decode_orciq.decode_capture`. Record elapsed host milliseconds, valid packet IDs/plaintext summaries, rejected packets, and a host pass/fail result. Derive differential class only when native evidence is known.

- [x] **Step 4: Replay all local captures**

Run:

```powershell
python tools/lora_lab/corpus_manifest.py `
  --manifest tools/lora_lab/corpus_manifest.json `
  --corpus-root artifacts/lora_validation/corpus `
  --output artifacts/lora_validation/corpus/differential_report.json
```

Expected: every file/header/hash verifies; controlled entries recover their expected packet ID and text; report leaves unavailable native results explicit.

### Task 3: Differential engineering record

**Files:**
- Modify: `docs/LORA_INDEPENDENT_VALIDATION.md`

**Interfaces:**
- Consumes: verified manifest and differential report.
- Produces: authoritative A/B/C/D matrix and exact identification of the approximately 73-second captures.

- [x] **Step 1: Add the evidence table**

Record capture ID, classification, expected packet ID, host result/time, native result/stage/time, and evidence source. State which entries are permanent regression vectors.

- [x] **Step 2: Run verification**

Run the LoRa tests, decoder self-test, manifest replay, Documentation Truth, `git diff --check`, and confirm raw IQ remains ignored.

- [ ] **Step 3: Commit the narrow slice**

```text
test(lora): catalog validation IQ corpus
```

Do not push yet; proceed next to a separately reviewed native timing-instrumentation slice.
