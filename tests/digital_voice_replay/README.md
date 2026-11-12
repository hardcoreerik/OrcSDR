# OrcSDR Digital Voice — host replay and fixture verification (D0)

**Scope:** `tests/digital_voice_replay/**` only. Standalone Python 3.9+ standard-library tooling. No firmware integration, radio access, network downloads, vocoder or decoder dependencies.

This tracks the **private** research collection in `hardcoreerik/REA-Results` at `chatgpt/digital-voice-rsa-rea-2026-11-01`, under `inbox/fixtures/digital-voice/`. The committed `manifest.json` contains *only metadata*, not IQ. All 11 entries are pinned by byte length and exact Git blob SHA-1. SHA-256 is pinned for the two generated controls; for the nine real-air captures, their SHA-256 is **unmeasured in the source manifest**. The tool computes local SHA-256 when files are available and reports it, but must **not claim a pinned SHA-256 match** for those nine entries.

## What this foundation proves (and doesn't)

- Proves byte integrity, format agreement, sample counts and repeatable **host-only** streaming/conversion for IQ that passes validation.
- Uses little-endian interleaved **CF32** (float32 I,Q; **8 bytes** per complex sample) for DMR Tier III and P25 Phase I, and little-endian interleaved **CS16** (signed int16 I,Q; **4 bytes** per complex sample) for DMR simplex, NXDN and synthetic negative samples.
- CS16 is normalized as `value / 32768.0` when read into Python floats. Native CF32 floats are **not** divided by 32768. CF32-to-CS16 output rounds to the nearest integer (Python ties-to-even) and clips to [-32768,32767]. Both formats preserve the I,Q ordering and 48-kS/s sample rate; no resampling, filtering or actual voice decoding occurs.
- Raw `.cfile`/`.cs16` do **not** have intrinsic format tags. Suffix detection is a **collection-specific hint**, not proof; manifest metadata is mandatory for verified replay. Unknown extensions require explicit format metadata. Payloads with WAV/RIFF headers are rejected. Alignment alone can **never** distinguish CF32 from CS16: any CF32 byte count is also a valid CS16 byte count.
- `replay` verifies the pinned hashes **before** yielding IQ chunks to a callable and returns IQ statistics only. It does **not** validate DMR sync, P25 NAC, NXDN frame counts, CRC/FEC, trunking or synthesized speech. `upstream_expected` records the external project's test assertions, **not OrcSDR results**.
- No P25 Phase II live-air IQ or P25 Phase I CQPSK IQ exists in this local research corpus. A reference P25 Phase II **dibit** file is *post-demodulation*, **not IQ**, and is intentionally excluded from this manifest.

## Install / test (Linux, macOS, Windows)

Run from the OrcSDR repo root. Nothing beyond Python 3.9+ is needed:

```bash
python3 -m unittest discover -s tests/digital_voice_replay -p 'test_*.py' -v
python3 tests/digital_voice_replay/replay.py list
```

Windows PowerShell equivalent:

```powershell
py -3 -m unittest discover -s tests/digital_voice_replay -p 'test_*.py' -v
py -3 tests/digital_voice_replay/replay.py list
```

Host tests generate small temporary IQ streams (in the OS temp directory) for CF32, CS16, conversion, hash tampering, path escape, incomplete frames, invalid floats and no-overwrite behavior. They **never commit or upload** the generated data. The real-corpus test intentionally skips unless `ORCSDR_DV_FIXTURES_ROOT` is set.

## Run against the private REA fixture collection

Keep the private research branch in its **own worktree or clean checkout**. Do not merge or copy raw IQ into OrcSDR. For example, from a local checkout of REA-Results:

```bash
git -C /path/to/REA-Results fetch origin chatgpt/digital-voice-rsa-rea-2026-11-01
git -C /path/to/REA-Results worktree add /path/to/REA-Digital-Voice origin/chatgpt/digital-voice-rsa-rea-2026-11-01
```

Set the fixture root to `/path/to/REA-Digital-Voice/inbox/fixtures/digital-voice` (the directory containing `DMR/`, `P25/`, `NXDN/`, `AUTO/`).

```bash
ROOT=/path/to/REA-Digital-Voice/inbox/fixtures/digital-voice
python3 tests/digital_voice_replay/replay.py verify --fixtures-root "$ROOT" --json
python3 tests/digital_voice_replay/replay.py verify --fixtures-root "$ROOT" --id dmr-tier3-control
python3 tests/digital_voice_replay/replay.py replay --fixtures-root "$ROOT" --id nxdn48-voice --chunk-samples 257
ORCSDR_DV_FIXTURES_ROOT="$ROOT" python3 -m unittest discover -s tests/digital_voice_replay -p 'test_*.py' -v
```

Windows PowerShell:

```powershell
$root = 'F:\AI\REA-Digital-Voice\inbox\fixtures\digital-voice'
py -3 tests/digital_voice_replay/replay.py verify --fixtures-root $root --json
py -3 tests/digital_voice_replay/replay.py replay --fixtures-root $root --id p25-phase1-city-control
$env:ORCSDR_DV_FIXTURES_ROOT = $root
py -3 -m unittest discover -s tests/digital_voice_replay -p 'test_*.py' -v
```

`verify` fails closed (exit 2) if any requested file is missing, altered or the wrong length, reports source SHA-256 for local audit and checks source-pinned Git blob SHA-1 (Git hashes `b'blob ' + size + b'\0' + content`). With no local IQ, `list` and all self-contained tests still work. Never interpret a skipped real-IQ test as an integrity pass.

## Optional conversion (local files only)

```bash
python3 tests/digital_voice_replay/replay.py convert \
  --fixtures-root "$ROOT" --id dmr-directmode-keyup \
  --to cf32-le --output /tmp/dmr-directmode-local.cfile
```

**Warning:** Output is IQ and may contain captured radio traffic. Conversion requires successfully verified source bytes, never overwrites an existing path, and refuses any destination under this tool's source directory. Put private output *outside the OrcSDR checkout*, and do not commit it. `convert` does not increase the signal's precision or create new information; CS16→CF32 converts integers to normalized float values; CF32→CS16 loses precision and can clip.

## How to integrate a decoder later

The reusable Python function `replay_iq(fixture, root, consumer, chunk_samples=4096)` invokes `consumer(chunk)` with ordered normalized `(I, Q)` pairs and returns hash-gated IQ statistics. A future decoder adapter can sit behind this function **in this directory** (or be developed by its own owner in another designated module); make independent protocol output assertions before marking any decoder gate as passed. Do not add firmware coupling to this replay foundation.

## Privacy / provenance / limitations

Source: pinned GopherTrunk commit `f06bfff493f47fdeb69807c8b08b2b32a253839f`, stored in the private REA-Results research branch above. The two AUTO controls are synthetic, not real-air; their SHA-256 values come from the REA fixture manifest. Captured radio-voice redistribution rights have not been established, even if the upstream program source is Apache-2.0. This public repository receives **only Python tests, documentation and metadata**. Binary files, dumps, recordings, logs and generated outputs must stay out of Git.

The research fixture collection has brief 0.8–2.0-second captures, not full calls or broad operational coverage. Hash checks prove byte identity to pinned captures, **not** that an actual P25, DMR or NXDN decoder works.
