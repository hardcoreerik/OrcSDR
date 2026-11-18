# ACARS reference provenance

No external RF code, databases, aircraft catalog, capture files, or third-party
fixtures were copied into this staged directory. All decoder/fixture sources
are referenced by their canonical paths in the existing OrcSDR repository.

Primary repository references (retrieved from integration branch
`codex/cardputer-zero-ui-audio-controls`, base `73736f9`):

- [`acars_core.hpp` / `acars_core.cpp`](https://github.com/hardcoreerik/OrcSDR/tree/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-tab5/ui): channel/frame `Message`, CRC16-Kermit validation and conservative repair,
  decoder callback, `Frontend240k` /10 FIR and 24 kS/s channel decoder.
- [`acars_selftest.hpp` / `acars_selftest.cpp`](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-tab5/ui/acars_selftest.cpp):
  deterministic on-channel CU8 generator, with one fixed block and phase/noise
  test cases. Linked directly for the host tests; no generator fork delivered.
- [`live_capture.cpp`](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-cardputer-zero/src/live_capture.cpp): Linux path's exact 240,000 CU8 sample-rate contract and `+60 kHz` ACARS carrier, queue/gap behavior.
- [`iq_queue.hpp`](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-cardputer-zero/src/iq_queue.hpp): host IQ provenance and generation/index/drop fields.
- [`compact_renderer.hpp`](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/apps/orcsdr-cardputer-zero/src/compact/compact_renderer.hpp): existing native 5×7 glyphs reused without vendoring.
- [`UI_RENDERING_STANDARD.md`](https://github.com/hardcoreerik/OrcSDR/blob/codex/cardputer-zero-ui-audio-controls/docs/UI_RENDERING_STANDARD.md): partial widget updates, display ownership and physical-flicker validation contract.

The upstream ACARS decoder source identifies itself as
`SPDX-License-Identifier: AGPL-3.0-only`. This staged implementation uses the
same header on authored `.cpp`/`.hpp` files and does not remove or alter the
upstream notices. No aircraft traffic claim is derived from outside sources;
DEMO text is authored placeholder data and always labeled DEMO.
