# Digital Voice Replay — D0 handoff

- Ownership: `tests/digital_voice_replay/**` only; no firmware or production P25 modified.
- Input: 11 pinned IQ fixtures in private `hardcoreerik/REA-Results` branch `chatgpt/digital-voice-rsa-rea-2026-11-01` at `inbox/fixtures/digital-voice/`.
- Output: `manifest.json` + `replay.py` + stdlib unit tests. **No raw IQ committed.**
- Host verification: `python3 -m unittest discover -s tests/digital_voice_replay -p 'test_*.py' -v`.
- Real corpus verification: `python3 tests/digital_voice_replay/replay.py verify --fixtures-root /path/to/REA-Digital-Voice/inbox/fixtures/digital-voice`.
- Important: 9 real-air fixtures have Git blob SHA-1 source pins but no independently measured SHA-256 in the source manifest; 2 synthetic controls have both.
- IMPORTANT: Do not report decoder acceptance, sync/FEC/CRC, audio intelligibility, P25 Phase II RF support, or cross-device performance from IQ inspection. Upstream expected thresholds are metadata only.
- Next phase: pair a host decoder adapter with the manifest ID and capture independent decoder-event traces, lock timing, CRC/FEC and false-positive results, while keeping real IQ private and external to OrcSDR.
