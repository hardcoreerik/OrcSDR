# VDL Mode 2 research — OrcSDR Tab5
Date: 2026-10-28. Baseline: `claude/acars-data-decoding-6e3ed0` at `c11cbaae0ad0d0462e68d195b529e30945f1bdcb`.
**Status: RESEARCH + HOST-TESTED AVLC/ACARS EXTRACTION ONLY. No off-air decode, RS or physical receiver yet. Receive-only.**

## Sources and priority
- ETSI EN 301 841-1 V1.4.1 (2015), §§5.2–5.4: https://www.etsi.org/deliver/etsi_EN/301800_301899/30184101/01.04.01_60/en_30184101v010401p.pdf
- ICAO Doc 9776, 2nd ed.: https://store.icao.int/en/manual-on-vhf-digital-link-vdl-mode-2-doc-9776 (not acquired; paid publication).
- Applicable ICAO Annex 10 Volume III VDL SARPs; ARINC 618 (ACARS over AVLC), ARINC 631 (AVLC), EUROCAE ED-92B / RTCA DO-281.
- FCC Part 87 / 47 CFR §2.1047: equipment requirements, not the receiver DSP protocol specification. Search-indexed Airtel ATN MTP-100 test: https://fcc.report/FCC-ID/2ATN6-MTP100/4500172.pdf (PDF not acquired).
- `szpajder/dumpvdl2` commit `686e87f2294ef6275855ba7364bd4bb9a0f8f56a`; `TLeconte/vdlm2dec` commit `b47873dca7b20584db3cbef646f2d09f450cf071`; `szpajder/libacars` commit `9af09a0121d4ec577339cbd4c7420d7519da48fa`. Inspected as protocol references: no third-party source imported.

## Constant provenance ledger
Verified = directly confirmed in ETSI or in multiple independent credible documents; reference-confirmed = checked against a real open-source decoder's code, *not* independently verified from off-air IQ; assumed = project choice; unknown = not yet established.

| Constant | Value | Status | Source |
|---|---|---|---|
| Modulation and tribit direction | differential 8PSK, 3 bits per symbol, LSB-first | Verified | ETSI §5.3 |
| Symbol rate | 10,500 symbols/s | Verified | ETSI §6.1.6, WAVEcom paper p.4 |
| Raw bitrate | 31,500 bits/s (not net throughput) | Verified | ETSI, Honeywell VDL2 radio product page |
| Raised-cosine excess bandwidth | alpha = **0.6 nominal** | Verified | ETSI §5.3; FCC MTP-100 search-indexed transmitter report |
| Ramp/sync/reserved | 5/16/1 symbols | Verified | ETSI §5.4 |
| Transmission length / header FEC | 17 bits / 5 bits | Verified | ETSI §5.4 |
| Channel spacing | 25 kHz | Verified | ETSI §5.6 |
| Common signalling frequency | 136.975 MHz | Verified | dumpvdl2 default; WAVEcom VDL2 paper p.3 |
| Specific sync phase word | 16 phases in `dumpvdl2/src/demod.c` | Reference-confirmed | Phase vector, not yet measured against capture |
| D8PSK Gray map | {0,1,3,2,6,7,5,4} in phase-step order | Reference-confirmed | dumpvdl2 `demod.c` |
| Scrambler | 15-bit LFSR; IV 0x6959; polynomial x^15+x+1 | Reference-confirmed | dumpvdl2 `decode.c`, `bitstream.c` |
| RS(255,249) | 6 parity bytes | Reference-confirmed | dumpvdl2 `dumpvdl2.h`, WAVEcom p.4 |
| RS polynomial / first root / prim | 0x187 / 120 / 1, 8-bit symbols | Reference-confirmed | dumpvdl2 `rs.c`: `init_rs_char(8,0x187,120,1,6,0)` |
| Short final RS parity | <3 bytes => 0, 3–30 => 2, 31–67 => 4, 68+ => 6 | Reference-confirmed | dumpvdl2 `decode.c` |
| Interleaver | row/column RS block interleave | Reference-confirmed | dumpvdl2 `decode.c` |
| AVLC flag + bit stuffing | 0x7e, zero after five consecutive ones | Reference-confirmed; synthetic host-tested | dumpvdl2 `bitstream.c` |
| AVLC CRC | 0x1021 / reflected 0x8408; init 0xffff; good residue 0xf0b8 | Reference-confirmed; synthetic host-tested | dumpvdl2 `crc.c` and `avlc.c` |
| Inner ACARS BCS | reflected 0x8408, init 0, residue 0 (exclude DEL) | Reference-confirmed; synthetic host-tested | libacars `crc.c` and `acars.c` |
| ACARS over AVLC prefix | FF FF 01 in AVLC I-frame INFO | Reference-confirmed | dumpvdl2 `avlc.c` |
| ACARS fields | mode, tail(7), ack, label(2), block, optional STX+text, ETX/ETB, BCS(2), DEL | Reference-confirmed | libacars `acars.c` / OrcSDR `acars_core.cpp` |
| VDL2 proposed input | 250 kS/s complex IQ | Assumed deployment target | User requirement; actual negotiation untested |
| Proposed channel DSP rate | 50 kS/s after /5 | Assumed | 50k/10.5k=4.7619 samples/symbol, needs fractional clock |
| Fixed OrcSDR AVLC buffer cap | 2048 bytes | Assumed | Implementation memory bound, not a standard max |
| Header FEC parity-check masks | 0x01FFF0, 0x07E1FE8, 0x18E61E4, 0x1B6A662, 0x0D3CAA1 | Reference-confirmed; synthetic host-tested | dumpvdl2/src/decode.c `H[5]`, xng header.rs; confirm vs IEC/ICAO edition before calling standard-verified |
| HDLC stuffed bit buffer cap | 2048 + floor(2048/5) + 16 = 2473 octets | Assumed implementation capacity, not protocol limit | Worst-case five-one stuffing overhead plus slack in vdl2_frames.hpp |
| RX RRC implementation choice and taps | **Not settled; A/B filter OFF vs ON** | Unknown (receiver design) | ETSI TX α=0.6; xng captured-IQ reports RX RRC harms demod; see cross-check below |
| RS octet packing | LSB-first VDL2/HDLC wire order | Reference-confirmed | dumpvdl2 bitstream.c, xng VDL2 notes; verify against real RS vector |
| Exact preamble bit orientation / header syndrome | TBD until off-air vectors | Unknown | Need ETSI + independent capture oracle |

**Critical trap:** A hobby protocol wiki describes a *different* Golay(24,12) header, 3-octet AVLC addresses, and scrambler/RS polynomial. It conflicts with ETSI's 17+5 header and the verified dumpvdl2 source. Do NOT use those values without proving waveform compatibility.

## Software landscape and differentiators
- **dumpvdl2** (GPL-3.0-or-later): strongest mature free receiver and primary reference oracle; supports RTL-SDR/SDRplay/Mirics/SoapySDR and replay, multiple channels, RS/AVLC/AOA, ATN protocols via libacars, X.25 and ACARS reassembly, hex, JSON, binary archival, signal strength/noise/ppm diagnostics. Its README documents real-user concerns: poor antenna view, gain/clipping, dongle ppm, lost USB samples and CPU load on Raspberry Pi. https://github.com/szpajder/dumpvdl2
- **vdlm2dec**: older lightweight RTL-SDR and Airspy-compatible VDL2 decoder; maintained less actively; Airframes recommends dumpvdl2 for new users. https://github.com/TLeconte/vdlm2dec ; https://docs.airframes.io/docs/decoders/install-vdlm2dec/
- **airframesio/xng**: newer Rust multi-mode receiver; its own CI benchmarks report 44 VDL2 frames versus dumpvdl2's 41 on one shared off-air capture. Treat the count as **self-reported benchmark, not independent ranking**. https://github.com/airframesio/xng
- **WAVEcom W-CODE** commercial analyzer: native D8PSK, AVLC, AOA, ATN observation with IQ and text UI; research public description, not software/binaries. https://www.wavecom.ch/content/pdf/advanced_protocol_vdl-m2.pdf
- **MultiPSK/SDRangel**: adjacent SDR workflows; a native complete VDL2 parity-equivalent demodulator was **not verified** this session.
- **Honeywell RTA-44D / RTA-50D** actual airborne VDRs, voice/POA ACARS/VDL2/CPDLC: https://www.honeywellaerospace.com/us/en/products-and-services/products/cabin-and-cockpit/radios/high-frequency-radios/vhf-data-link-vdl-mode-2-radios
- **Collins Aerospace VHF-2100** airborne transceiver with VDL2, fitted on commercial aircraft: https://www.rtx.com/collinsaerospace/what-we-do/industries/commercial-aviation/flight-deck/communications/voice-and-data/vhf-2100-very-high-frequency-transceiver
- **Airtel ATN MTP-100** FCC transmission test (index-confirmed, PDF not inspected) and **ground network operators** Collins/ARINC, SITA, L3Harris cited by FAA: https://www.faa.gov/uas/advanced_operations/upper_class_etm/ETM-Comm-Technologies.pdf
- User reports: dumpvdl2 documents high placement/clear RF view, measured SNR/ppm and no-clip gain as critical. No independently controlled user comparison was obtained. https://docs.airframes.io/docs/decoders/install-dumpvdl2/

## Target pipeline and implementation gates
`RTL-SDR CU8 250k -> channel mixer, DC/CFO correction -> anti-alias /5 to ~50k -> selectable RX filter (start without RRC; A/B test alpha .6 RRC) -> fractional symbol clock & D8PSK phase demod -> 16-symbol sync -> 17-bit length/header FEC -> descrambling -> RS/deinterleave -> AVLC flag/unstuff/CRC -> marker & ACARS BCS -> acars::Message -> existing dashboard's SPSC event queue -> interpreter.`

Existing 2400-bps AM-envelope/MSK ACARS path MUST NOT be reused for D8PSK demod; share UI records and interpreter, not DSP. Preserve ATN/CPDLC/ADS-C as explicit raw hex, no invented message translation. Avoid `main.cpp`. Receive-only.

**Committed first slice:** `apps/orcsdr-tab5/ui/vdl2_frames.{hpp,cpp}`, `tests/vdl2_frames_test.cpp`; stand-alone AVLC bit/byte framing, CRC, AOA-to-`acars::Message` on *both* valid CRC layers, borrowed raw other frames. No allocation in production hot path and fixed buffers. **NOT yet linked into firmware**.

**Host test:** GCC 14.2 C++17 strict flags and ASan/UBSan passed in isolated sandbox with locally mirrored `acars::Message` stub; synthetic AVLC+ACARS, raw, both CRC rejection paths, overflow. Production header/firmware build, hardware, real captured IQ, RS and symbol recovery remain UNTESTED.

```sh
mkdir -p /tmp/orcsdr-vdl2
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I apps/orcsdr-tab5/ui apps/orcsdr-tab5/ui/vdl2_frames.cpp tests/vdl2_frames_test.cpp \
  -o /tmp/orcsdr-vdl2/vdl2_frames_test
/tmp/orcsdr-vdl2/vdl2_frames_test
```

**Next acceptance gates:** 1. capture pinned real 136.975-MHz IQ and reference dumpvdl2 AVLC hex/counts; 2. independent reference vectors for header/RS and bit order; 3. offline IQ demod symbol/FEC modules with deterministic replays and CFO/SNR robustness; 4. profile CPU/RAM/USB continuity on Tab5; 5. wire approved receiver callback to existing dashboard and add POA/VDL2 source indicator plus raw-hex view, maintaining 5-inch UI readability; 6. flash/test only on authorized hardware. No valid on-air VDL2 decodes are yet claimed.

## Critical independent waveform cross-check (airframesio/xng; recorded 2026-10-28)
**Do not assume a receiver root-raised-cosine (RRC) filter is appropriate.** ETSI EN 301 841-1 §5.3 specifies the *transmitted* raised-cosine spectrum with alpha 0.6. The independently developed `airframesio/xng` receiver reports that the **transmitter output is already full raised-cosine (Nyquist)**; adding an RRC on receive is then not the matched half of a TX-RRC/RX-RRC pair, and can introduce ISI and lose frames. Its authors report a captured-IQ experiment where an added RX RRC dropped frame count from 17 to 1. This is a *project-reported off-air result*, not an independently repeated OrcSDR measurement. Sources: https://github.com/airframesio/xng/blob/master/docs/notes/VDL2.md and https://github.com/airframesio/xng/blob/master/docs/notes/BENCHMARKS.md (check current default branch before reusing literal main). **Our default experimental candidate should be a channel anti-alias/filtering path *without* additional RRC; RRC-on and RRC-off must be compared with identical off-air IQ and a CRC-valid AVLC frame count oracle.** The user's RRC requirement is thereby addressed in the filter-design decision and A/B tests, not blindly implemented.

Another reproducibility trap documented by xng: **Reed-Solomon symbols assemble LSB-first from the VDL2/HDLC bitstream**. Feeding MSB-first reversed octets into otherwise correct RS arithmetic can appear to be total FEC failure. In xng's own captured-IQ benchmark, repairing this convention contributed to the reported 44 vs 41 valid-frame counts (xng vs dumpvdl2, single capture). Treat these numbers as benchmark authors' claims, not a broad decoder ranking. Their fractional interpolation at 100/105 kS/s and phase/CFO fit are also candidate approaches to benchmark on the P4, not constants in ICAO. See the xng VDL2 notes above.

## Second host-tested slice: header and descrambler (2026-10-28)
- `apps/orcsdr-tab5/ui/vdl2_burst.{hpp,cpp}` is an independent allocation-free C++17 module with `decode_burst_header`, stateful `Descrambler15`, `final_block_rs_parity` and LSB-first octet packing.
- `tests/vdl2_burst_test.cpp` generates synthetic header checkwords, checks 0 and 1-bit header error correction (unrepairable 2-bit syndrome rejected), and tests the x^15+x+1 feedback stream seeded 0x6959. Its first four all-zero-input keystream octets are C8 D8 23 A4; source is reference-confirmed from `dumpvdl2/src/bitstream.c` but vector is generated locally, **not an off-air captured bitstream**. Also verifies RS-shortening boundary counts and packed HDLC LSB-first bytes.
- Test compiled with GCC C++17, strict warnings, AddressSanitizer + UndefinedBehaviorSanitizer in a local standalone sandbox: **passed**. The header decoder is a bounded primitive, not an on-air-validated VDL2 header implementation; subsequent RS/AVLC CRC is required to disambiguate any header miscorrection. No RS error correction implementation, physical IQ demod, or firmware wiring has been added yet.
- HDLC stream input cap in `vdl2_frames.hpp` was increased to `kMaxAvlcBytes + kMaxAvlcBytes/5 + 16` to accommodate worst-case bit stuffing; overflow test now targets the larger cap and passed.

```sh
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
    -I apps/orcsdr-tab5/ui apps/orcsdr-tab5/ui/vdl2_burst.cpp tests/vdl2_burst_test.cpp \
    -o /tmp/orcsdr-vdl2/vdl2_burst_test
/tmp/orcsdr-vdl2/vdl2_burst_test
```

**Reproducible host entrypoint:** `bash tools/test-vdl2-host.sh` from repository root, building both C++17 modules against the checked-out real OrcSDR `acars_core.hpp`. This exact script was committed but was not executed against a GitHub checkout in the web session; the local standalone mirror passed ASan/UBSan.
