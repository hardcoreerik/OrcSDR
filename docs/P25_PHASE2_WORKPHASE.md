# P25 Phase II work phase

> **Historical Evidence / superseded work plan.** This records the branch and
> gate sequence used to land the current Phase II transport, sync, complete
> burst retention, and DUID classification stages. Branch names below are not
> the current worktree. Current gaps are payload decode and AMBE+2 audio; see
> [`PROJECT_STATUS.md`](../PROJECT_STATUS.md) and [`Roadmap.md`](../Roadmap.md).

This work adds clear-voice P25 Phase II in small, independently testable pull
requests while preserving the working Phase I receiver. Public status remains
**P25 WIP** until live Phase II audio passes.

## Completed pull request: grant transport

Branch: `codex/p25-phase2-transport`

This pull request corrects TDMA carrier/slot mapping, preserves the decoded
grant and system identity, and adds a fixed-memory 6,000-symbol/s traffic
burst detector. Normal operation reports a Phase II grant and remains on the
control channel. Authenticated trace mode performs a three-second transport
probe, reports burst synchronization or rejection, and returns to control. It
does not decode Phase II MAC or audio.

The shared 960 kS/s to 48 kS/s channelizer was already inside
`p25_decoder_core`; no structural move was needed. Phase I C4FM/CQPSK and IMBE
paths remain unchanged.

## Evidence ledger

| Gate | Result |
| --- | --- |
| Pre-change optimized and sanitizer host tests | Passed 2026-09-06 |
| TDMA carrier/slot and 6,000-symbol/s sync host tests | Passed 2026-09-06 |
| Native ESP-IDF 5.5.4 build | Passed 2026-09-06; final reviewed app image is 2,263,840 bytes with 46% app partition free; no C6 image embedded |
| Exact image flashed to COM17 | Passed 2026-09-06; final reviewed image SHA-256 `B491F9B3E5B7DA482DA25B36066AF0F31E2B5D214AE2E12133DE5F9DE669ECF0`; flash verification, normal boot, dashboard regression, and live control-channel status passed |
| Boot memory regression | Caught before acceptance: the first candidate exhausted the internal DMA reserve. Moving the existing 2,376-byte P25 waterfall scratch row to configured PSRAM restored the reserve; the final exact image boots normally. |
| Dashboard and driver regression | Passed 2026-09-06; full UI workflow and driver 0.7.9 checks passed with zero USB, IQ, and audio drops |
| OSRP control-channel identity | Passed 2026-09-06 at observed Quarry Hill control `770.66875 MHz`: WACN `9254A`, SYSID `00A`, RFSS/site `6/6`; the frequency is observed evidence, not a permanent hard-coded assignment |
| Real OSRP TDMA grant and correct carrier/slot | Passed 2026-09-06: TGID `38130`, channel ID `8`, observed carriers including `773.28125 MHz`, slot `0`, zero mapping errors |
| Traffic-channel burst sync and control return | Passed 2026-09-06: live 38130 burst synchronized after 614 symbols with zero sync-word bit errors; control return and relock passed |
| Continued Phase I technical path | Passed 2026-09-06 on Lane County: 20 grant events, 528 IMBE frames, 506,880 PCM samples, repeated control returns/relocks, stable heap, adequate stack headroom, encrypted-call mute/return, and zero USB/IQ/audio drops |
| Exact-image Phase I audio and visual acceptance | Passed 2026-09-06: user confirmed clear audio on TGIDs `20203`, `20391` (`LCF Firecom 1`), and `38130`; supplied photos show the P25 Monitor rendering normally while following the calls |

The final review correction makes `RTL_P25_STATUS` report
`follow=phase2_probe` during a diagnostic traffic-channel probe and retains the
associated grant in the dashboard snapshot. This prevents automation and UI
status from mistaking an active probe for a completed control-channel return.

The 90-second OSRP acceptance run used the local watchlist `38130`, `40253`,
`38131`, `40251`, `40252`, `40254`, `40255`, and `20165`, while allowing any
live Phase II talkgroup to satisfy the transport gate. The selected grant,
acquisition, synchronization, return, and relock were required to use the same
TGID. The result was `PASS` with a 22,930,276-byte heap floor and no USB, IQ,
audio, or voice-queue drops. Local frequencies, aliases, and raw traffic IQ
remain untracked.

The Lane County receiver also presented numeric TGID `38130` as clear Phase I
voice at `453.2375 MHz`, while OSRP presented numeric TGID `38130` in a live
Phase II grant. This confirms that a talkgroup number must always be associated
with its decoded system identity and call mode; the number alone is not a
globally unique identity.

## Standards, source, and legal record

The official TIA TR-8 documents named in the approved implementation plan are
the normative source. Their PDFs were not present locally on 2026-09-06, so
this repository records their identifiers and links without claiming revision
hashes or redistributing copyrighted files. Exact revisions and SHA-256 hashes
will be added after the project owner obtains the PDFs through TIA's terms.

OP25 commit `71abcd0ead32f86f51615ea6cc8a6a4dba4c949a` was consulted for TDMA
channel-to-carrier behavior and the 40-bit synchronization constant. The
reviewed files are GPL-3.0-or-later. DSD-FME commit
`cd5f0f4a8e285aafd7d39194cb7c3e423375a0f8` was consulted to cross-check the
6,000-symbol/s framing model; its `COPYRIGHT` describes ISC terms for most code
and GPL-2.0 for named inherited files. OrcSDR incorporates no source code from
either project in this work; the small receiver implementation and tests were
written independently.

The project owner confirmed on 2026-09-06 that the separate legal gate for
Phase II audio distribution is approved. This engineering record does not
identify the reviewer or reproduce legal advice, so it does not claim an
independent legal opinion. Encryption remains detection-and-mute only; this
work adds no keys, key storage, affiliation, transmission, or decryption.

## Current worktree: complete burst and DUID foundation

Branch: `codex/p25-phase2-protocol-core`

The next protocol slice extends the existing fixed-memory Phase II receiver. It
collects a complete 180-dibit traffic burst after synchronization, normalizes
reverse polarity, extracts the four separated DUID dibits, and decodes the
extended Hamming codeword without a lookup table. Status distinguishes complete
and truncated bursts, corrected DUID errors, voice burst types, control burst
types, and unsupported or invalid types.

This boundary matters because a synchronization hit alone does not prove that
OrcSDR retained a complete TDMA burst. The live gate now requires a complete
burst. It still does not claim descrambling, MAC decode, AMBE+2 output, or Phase
II audio.

Host tests passed in optimized and ASan/LSan/UBSan builds on 2026-09-06. The
final native ESP-IDF 5.5.4 build passed with an application size of 2,266,112
bytes and 46% of the application partition free. The exact flashed application
image has SHA-256
`52916B9C5C2B21E04C6AA5C2007C785A751438D705975E6219E2EF3835BD4614`.
Flash verification and normal boot passed on the Tab5, followed by the full
authenticated dashboard regression and RTL-SDR driver 0.7.9 regression. Both
reported zero USB, IQ, and audio drops.

The clean five-minute OSRP run passed at the observed control channel
`770.66875 MHz`. It decoded WACN `9254A`, SYSID `00A`, RFSS/site `6/6`, and NAC
`006`; observed 35 grant events; returned to and relocked the control channel;
and retained a 22,923,244-byte heap floor with no USB, IQ, audio, or voice-queue
drops. Live Phase II TGID `38130` produced complete bursts with valid DUID
classifications, including DUID `9` FACCH and DUID `3` SACCH control bursts. A
separate earlier observation produced DUID `0` four-voice classification, but
that run was excluded from the formal gate because the operator pressed Survey
during automation. The clean run is the acceptance result.

This evidence proves real traffic-channel burst capture, DUID correction, and
classification. It does not prove Phase II payload descrambling, MAC messages,
AMBE+2 frames, or audio.

## Remaining pull requests

1. Continue the hardware-independent Phase II protocol core with
   slot/superframe tracking, ISCH, scrambling, FEC, MAC, ESS, and bounded 72-bit
   vocoder frames. Field layouts will be checked against the locally obtained
   TIA documents before implementation.
2. Clear AMBE+2 synthesis through the existing bounded PCM callback.
3. Update every P25 dashboard view for Phase II. The monitor, spectrum/RF
   health, talkgroup, and status views must show Phase I or Phase II, TDMA slot,
   carrier, talkgroup/source identity, clear/encrypted state, decode quality,
   audio state, and the reason a call was not followed. Hold, skip, follow, and
   return-to-control behavior must work consistently for both phases. Remove the
   temporary `Phase II call detected` transport message only after live Phase II
   audio and dashboard acceptance pass.
4. Location-neutral P25 system discovery and confirmed profile saving.
