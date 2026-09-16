# Independent LoRa / Meshtastic validation

Status: first-milestone hardware baseline complete; second-milestone offline
candidate-detector benchmark started; trigger/DSP firmware unchanged.

## Provenance

This work starts from OrcSDR `b6e7761f8cd6717ead133ed9a827c90f26a6f0b9`
on 2026-09-12. OrcSDR pins `esp-rtl-sdr`
`b175dfea6782faa97e512d4a2408767c75977527`, which is also the freshly fetched
head of that repository's `origin/master`. The driver repository has no
`origin/main`; `origin/master` is its advertised default branch.

The external investigation is prompted by measurements and experiments in
Hitman90210/OrcSDR. The fetched fork head is
`465039ecd87187c657757624f41c33e193540b9b`. No fork commit was cherry-picked
and no fork source was copied.

The principal external hypotheses to reproduce are:

| Fork commit | Reported observation; not yet independently reproduced |
|---|---|
| `81400e5343a6c914289b4bd311aea114fa5d7f58` | 24/24 false triggers in a 180-second sample, with 1.8-3.1 seconds of decode work per trigger and no preambles. |
| `72a12d8947191f4469c0276f02c9e692c468acd5` | A channel-energy discriminator reportedly reduced false triggers from 24 to 3-6 per 180 seconds. |
| `1712532ed48778f17f40a50b0d407ba19f1c2221` | The existing self-test did not exercise the DSP path; a synthetic chirp/resampling matrix exposed a shared-scratch race. |
| `7c4fa1e4bbd8c3e02efc19d8788151dc42ec694d` | An internal grid of twice the LoRa bandwidth reportedly supports 125, 250, and 500 kHz without increasing the 960 kS/s capture rate. Hardware results in that commit are synthetic DSP results, not an RF payload decode. |
| `560032272ffad6c7af98ae5cb2b20729559639a6` | Long-interleaved headers can reportedly be distinguished, but complete LI payload/FEC/CRC support remained open. |

Current upstream source inspection confirms that `lora_iq_offer()` still makes
its initial capture decision from `rtl_signal_dbfs`, the wide receive-window
level. That makes the false-trigger hypothesis technically plausible; source
inspection is not a reproduction measurement.

## Initial device and software inventory

Read-only identity probes produced the following observations. No Meshtastic
configuration was changed and no test packet was transmitted.

| Role | Observed port | Identity | Firmware | Current safe config observation |
|---|---:|---|---|---|
| Controlled TX | COM24 | Heltec V4 | `2.8.0.47db0e3` | Region enum 1 (US), modem preset enum 0 (LongFast), automatic channel, no frequency override. |
| Reference RX | COM16 | LILYGO T-Beam S3 Core | `2.7.26.54e0d8d` | Region enum 1 (US), modem preset enum 0 (LongFast), automatic channel. |
| Experimental RX | COM17 | M5Stack Tab5 / OrcSDR | current upstream build | US LongFast slot 20, 906.875 MHz, automatic capture enabled. |
| Unrelated USB serial | COM30 | USB Billboard device, VID `057E`, PID `0000` | unknown | It did not answer the read-only `RTL_STATUS` query and is not treated as OrcSDR. |

The PC has Meshtastic Python `2.7.11` and pyserial `3.5`. The Heltec firmware
release maps to the official Meshtastic firmware commit
[`47db0e3020a608e06fb65cce70cd2f093021bd82`](https://github.com/meshtastic/firmware/commit/47db0e3020a608e06fb65cce70cd2f093021bd82)
and release
[`v2.8.0.47db0e3`](https://github.com/meshtastic/firmware/releases/tag/v2.8.0.47db0e3).
The installed Python protobuf enumerates 14 presets, while this firmware source
also advertises newer profile-specific presets. The complete matrix must come
from the device's 2.8 region/preset map rather than the older client enum.

Two simultaneous Meshtastic devices materially improve the experiment: the
reference node can prove that the controlled text was actually received while
OrcSDR is evaluated independently. The baseline harness therefore requires all
three views.

## First-milestone harness

`tools/lora_lab/run_suite.py`:

- records USB inventory and confirms each explicitly assigned device identity;
- refuses to configure either Meshtastic radio;
- refuses OTA transmission without an explicit local-legality confirmation;
- verifies both radios already share US LongFast and the same primary channel,
  without retaining or printing the channel key;
- verifies the OrcSDR LoRa plan and automatic PSRAM capture state;
- records a quiet interval for false-trigger measurement;
- sends sequence-numbered `ORC-LORA-TEST-000001` messages;
- records only matching test payloads from the reference receiver;
- correlates OrcSDR capture/decode events within non-overlapping host-time
  windows; and
- saves raw logs plus JSON, CSV, configuration, inventory, and Markdown output
  under `artifacts/lora_validation/<timestamp>/`.

The current OrcSDR serial event does not expose decoded packet ID/text, so the
first revision's OrcSDR correlation is explicitly time-window based. Exact
payload-level correlation will require retained IQ decoded on the host or a
bounded machine-readable packet event. It is not claimed yet.

## Evidence and current gate

Host parser/correlation self-check: pass.

Local inventory evidence was saved under:

- `artifacts/lora_validation/20260912-163129/`
- `artifacts/lora_validation/20260912-163345/`
- `artifacts/lora_validation/20260912-163350/`

The later inventories record the temporary USB disappearance rather than hiding
it. After reconnection, `artifacts/lora_validation/20260912-173152/` independently
identified all three roles and saved their non-secret configuration snapshots.

Four receive-only windows and three controlled LongFast runs are now recorded:

| Run | Duration / TX | Reference RX | OrcSDR RF | Preamble / header | CRC / Meshtastic | Zero-preamble false triggers | Drops |
|---|---:|---:|---:|---:|---:|---:|---:|
| `20260912-173415` | 900.0 s | n/a | 11 captures | 11 / 11 | 4 / 4 | 0 | 0 |
| `20260912-174945` quiet phase | 180.0 s | n/a | 1 capture | 1 / 1 | 1 / 1 | 0 | 0 |
| `20260912-174945` numbered TX phase | 10 TX | 8/10 | 8/10 windows | 7/10 / 7/10 | 3/10 / 3/10 | n/a | 1 |
| `20260912-175758` quiet phase | 180.0 s | n/a | 5 captures | 5 / 5 | 2 / 2 | 0 | 0 |
| `20260912-175758` numbered TX phase | 10 TX | 9/10 | 9/10 windows | 9/10 / 9/10 | 4/10 / 4/10 | n/a | 0 |
| `20260912-180909` quiet phase | 180.0 s | n/a | 0 captures | 0 / 0 | 0 / 0 | 0 | 0 |
| `20260912-180909` numbered TX phase | 10 TX | 10/10 | 10/10 windows | 5/10 / 5/10 | 1/10 / 1/10 | n/a | 1 |

Across 1,440 seconds of receive-only observation, OrcSDR started 17 captures.
Every completed candidate contained a detected LoRa preamble, so the fork's
reported 24 zero-preamble triggers in 180 seconds were **not reproduced** in
this RF environment. Seven quiet-window candidates reached CRC-valid encrypted
Meshtastic packets, showing that uncontrolled LoRa traffic was present rather
than a truly silent RF channel.

The first two controlled runs used an MLA-30+ outdoors approximately 50 feet
from the LoRa devices. The comparison run used a 915 MHz whip indoors
approximately 10 feet from them. Cabling was shielded as stated by the operator.
Because antenna, distance, and placement changed together, these are whole
receive-setup results rather than a controlled antenna-only or calibrated-power
comparison.

With the MLA-30+ setup, COM16 received 8/10 and 9/10 while OrcSDR produced 3/10
and 4/10 CRC-valid Meshtastic packets. With the closer indoor whip, COM16
received 10/10 and OrcSDR triggered in all ten TX windows, but only five windows
reached a preamble and one reached a CRC-valid Meshtastic packet. The whip run
also recorded one `capture_buffer_waiting` drop. Its capture-to-decode average
was 7.986 seconds and p95 was 12.070 seconds, compared with 5.554 seconds and
6.038 seconds in the immediately preceding MLA-30+ run.

The OrcSDR wide-window readings also changed materially: mean TX-window
`signal_dbfs`/`noise_dbfs` were approximately -4.72/-29.40 for the preceding
MLA-30+ run and -20.08/-15.73 for the whip run. These are receiver diagnostics,
not calibrated RF power measurements. The observed result does not support a
simple closer-is-better conclusion and should be repeated before assigning a
cause.

### Randomized TX/control-slot comparison

To account for uncontrolled LoRa traffic, the next paired experiment used the
same deterministic randomized schedule for both receive setups: ten TX slots,
ten no-TX control slots, 20 seconds per slot, and seed `90210`. Meshtastic and
OrcSDR configuration snapshots match across the pair.

| Receive setup | Reference RX | TX RF | TX preamble | TX CRC | Control RF / preamble / CRC | Zero-preamble TX attempts | Drops | Average / p95 decode |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 915 MHz whip, indoor, ~10 ft | 10/10 | 10/10 | 2/10 | 1/10 | 0/10 / 0/10 / 0/10 | 9 | 0 | 7.609 / 15.665 s |
| MLA-30+, outdoor, ~50 ft | 10/10 | 10/10 | 10/10 | 5/10 | 3/10 / 3/10 / 0/10 | 2 | 2 | 5.613 / 13.334 s |

The whip's no-TX controls were empty, while all ten TX slots triggered OrcSDR.
This makes unrelated traffic an unlikely explanation for its nine
zero-preamble attempts in this run. The MLA-30+ observed RF/preambles in three
control slots, proving that its TX-window activity can be contaminated by
uncontrolled traffic; none of those control slots reached CRC.

The paired result favors the MLA-30+ setup for preamble and CRC yield, but it
does not isolate antenna performance: antenna type, placement, distance, and
test time differ. OrcSDR still lacks payload identity, so randomized controls
support statistical comparison rather than exact attribution of every
CRC-valid packet. A second counterbalanced pair is required before assigning a
physical cause.

OrcSDR does not yet emit decoded payload identity. Association to a numbered
TX therefore remains a non-overlapping host-time-window estimate. In the first
controlled run, two extra decode attempts occurred inside TX windows, but they
are not claimed as duplicate packet decodes. Its corrected
capture-to-completed-decode time averaged 6.197 seconds with an 8.466-second
p95 using matching OrcSDR capture sequence IDs.

Raw local evidence:

- `artifacts/lora_validation/20260912-173415/`
- `artifacts/lora_validation/20260912-174945/`
- `artifacts/lora_validation/20260912-175758/`
- `artifacts/lora_validation/20260912-180909/`
- `artifacts/lora_validation/20260912-182146/`
- `artifacts/lora_validation/20260912-183330/`

### IQ retrieval stability finding

After the paired runs, a manual four-second PSRAM IQ capture at 960 ksample/s
completed, but host retrieval ended early with a serial timeout. The subsequent
abort command also timed out, and the operator observed the Tab5 blue-screen
and reboot to Home at 906.875 MHz. No IQ file was retained.

The boot-time reset reason was not captured, so this is recorded only as an
unclassified reboot during sustained IQ retrieval. It is not attributed to a
watchdog, brownout, USB fault, or another cause. The same retrieval path should
not be repeated with multiple outstanding chunk requests.

The host transfer was subsequently changed to keep exactly one 2 KiB chunk
request outstanding and to tolerate transient empty serial reads within a
bounded deadline. No firmware or SD-card write was required. Two matched manual
captures then transferred directly from PSRAM to the PC without retry or reboot:

| Receive setup | Raw IQ | Transfer | SHA-256 | Host decode |
|---|---:|---:|---|---|
| MLA-30+, outdoor, ~50 ft | 7,680,000 bytes | 46.752 s | `7c6f476e9a8b6740f69b44ba985a19626f88ab1a7fad0ddb03dae2ed134767b7` | `ORC-IQ-MLA30-20260912-192454` |
| 915 MHz whip, indoor, ~10 ft | 7,680,000 bytes | 46.535 s | `66028e4a1e83f4337b025480f0f3d890823572dbe0e0cca2ff86de64cd2ce9d3` | `ORC-IQ-WHIP915-20260912-192815` |

The Tab5 native decoder completed the MLA-30+ capture in 12.445 seconds and the
whip capture in 72.252 seconds. The latter found two preambles and one CRC
failure; offline host decoding still recovered the labeled CRC-valid packet.
These timings are capture-specific performance evidence, not an antenna-only
comparison.

Matched no-controlled-TX captures were also collected. The MLA-30+ window had
no preamble in either native or host decoding. The whip window contained a
CRC-valid public-channel position packet from an uncontrolled node, so it is
labeled background LoRa rather than a negative vector.

### Initial staged-detector benchmark

`tools/lora_lab/candidate_detector.py` measures total-window power, the fraction
of FFT energy inside the configured LoRa channel, and repeated dechirped FFT
peaks. It uses only NumPy and the ORCIQ metadata; it does not call the full LoRa
decoder when computing these metrics.

| Capture | Power RMS p95 | In-channel ratio p95 | Consecutive chirps | Peak / median FFT |
|---|---:|---:|---:|---:|
| MLA-30+ controlled TX | 1.195940 | 0.987187 | 17 | 220.553 |
| Whip controlled TX | 1.196912 | 0.986759 | 16 | 215.044 |
| Whip no-controlled-TX, background LoRa present | 1.193266 | 0.982133 | 16 | 220.242 |
| MLA-30+ no-controlled-TX, no preamble | 0.048021 | 0.291400 | 2 | 3.368 |

This small corpus shows that power and channel occupancy identify RF activity,
while repeated chirp peaks distinguish the no-preamble capture from all three
LoRa-positive captures. It is not yet sufficient to choose a production
threshold or claim a false-positive rate. More negative interference classes
and weak-signal positives are required before firmware gating is enabled.

### Configured transmit-power pair

The Heltec was then configured for 15 dBm and 2 dBm for one controlled capture
on each receive setup. COM16 received all four exact tokens, and offline host
decoding recovered all four from their saved IQ. The Heltec was verified back
at 30 dBm and OrcSDR automatic capture was verified on afterward.

| Receive setup | Configured TX | Reference / host | Native result | Native time | Consecutive chirps |
|---|---:|---|---|---:|---:|
| MLA-30+, outdoor, ~50 ft | 15 dBm | exact / exact | preamble, CRC failure | 73.665 s | 17 |
| MLA-30+, outdoor, ~50 ft | 2 dBm | exact / exact | preamble, CRC failure | 73.738 s | 16 |
| 915 MHz whip, indoor, ~10 ft | 15 dBm | exact / exact | CRC valid | 12.485 s | 16 |
| 915 MHz whip, indoor, ~10 ft | 2 dBm | exact / exact | two CRC-valid packets in window | 12.638 s | 17 |

Power RMS p95 remained between 1.186538 and 1.196592 across these captures.
Thus the configured power change did not create a calibrated weak-signal series
at the SDR input in this setup. These files are useful positive vectors but
must not be presented as receiver-sensitivity evidence. Reproducible host-side
attenuation/noise impairment or a physically attenuated RF path is required for
that measurement.

### Deterministic offline impairment

The two original controlled captures were scaled from their measured power RMS
p95 to 0.05 RMS, then impaired with seeded complex Gaussian noise at 10, 5, 0,
-5, -10, -15, and -20 dB SNR. Seeds 90210-90216 were used for the MLA-30+
capture and 90217-90223 for the whip capture. One temporary ORCIQ file at a
time was quantized and passed to the full host decoder; no impaired IQ copies
were retained. Results are in the ignored lab artifact
`artifacts/lora_validation/corpus/impairment_benchmark.json`.

| SNR (dB) | MLA chirps / peak-to-median / host | Whip chirps / peak-to-median / host |
|---:|---|---|
| 10 | 17 / 115.257 / exact packet | 16 / 115.624 / exact packet |
| 5 | 17 / 73.430 / exact packet | 16 / 72.717 / exact packet |
| 0 | 17 / 43.907 / exact packet | 16 / 43.652 / exact packet |
| -5 | 17 / 26.168 / exact packet | 16 / 25.374 / exact packet |
| -10 | 17 / 14.291 / exact packet | 16 / 14.395 / exact packet |
| -15 | 17 / 8.874 / exact packet | 16 / 9.068 / exact packet |
| -20 | 17 / 5.705 / exact packet | 16 / 5.440 / exact packet |

Measured SNR was within 0.01 dB of every requested point. No clipping occurred
through -10 dB; -15 dB clipped at most 0.000156% of complex samples and -20 dB
clipped at most 0.951016%. Both controlled packet IDs remained recoverable at
every point, so this range does not establish a host-decoder failure boundary
or justify a production candidate threshold. These are deterministic synthetic
algorithm results, not calibrated RF sensitivity or antenna-performance data.

A coarse extension using the same 0.05 RMS target failed at -25 dB but clipped
about 21% of samples, so that apparent boundary was rejected. A refined scan
used the new `--target-rms 0.02` control. Five deterministic seeds per receive
setup were then run at each transition point (90500-90514 for MLA-30+ and
90515-90529 for the whip):

| SNR (dB) | MLA host decode | Whip host decode | MLA chirps | Whip chirps |
|---:|---:|---:|---:|---:|
| -21 | 5/5 | 5/5 | 13-17 | 16-17 |
| -23 | 3/5 | 4/5 | 5-16 | 9-16 |
| -25 | 1/5 | 0/5 | 3-9 | 3-10 |

Maximum clipping was 0% at -21 dB, 0.000234% at -23 dB, and 0.014844% at
-25 dB. This bounds the synthetic transition but still does not justify a
production threshold because the corpus does not yet cover non-LoRa
interference classes.

### Matched receive-only negative pair

One additional manual four-second receive-only capture was collected on each
antenna setup without a controlled transmission. Both host decodes reported no
LoRa preamble. Direct PSRAM-to-PC transfer completed without an SD write or
device reboot, and automatic capture remained enabled afterward.

| Receive setup | Power RMS p95 | In-channel ratio p95 | Consecutive chirps | Peak / median FFT | SHA-256 |
|---|---:|---:|---:|---:|---|
| MLA-30+, outdoor, ~50 ft | 0.048597 | 0.290769 | 2 | 3.368 | `38e1d39b93c2ab9e6e47b0bf6d3e395061268ff9b86e2b438424554cbda16a8f` |
| 915 MHz whip, indoor, ~10 ft | 0.149974 | 0.272990 | 2 | 3.457 | `072fbd6a17a2924e1630c965dd61df8b13aa5d3698891e82a2ee1c5d1b20771c` |

Together with the earlier MLA-30+ control, the corpus now has three confirmed
no-preamble captures across both receive setups. The higher whip power did not
produce repeated-chirp evidence. This remains too small to estimate a field
false-positive rate.

The first attempt to retain an automatic energy-triggered capture exposed a
current ownership boundary: firmware hands that buffer directly to native
decoding after `RTL_IQ_DONE`, so the host watcher received
`RTL_IQ_RETRIEVE_ERROR capture_not_ready`. Manual capture was used for the
matched pair. No automatic capture was represented as retained IQ.

### Corpus manifest and host/native differential baseline

`tools/lora_lab/corpus_manifest.json` assigns content-bound IDs to all ten
useful captures. It records SHA-256, ORCIQ metadata, ground truth, packet ID and
plaintext where controlled, reference/host/native results, setup, and evidence
provenance. Raw IQ remains only in the ignored local directory
`artifacts/lora_validation/corpus/`; it is not committed to Git. The verifier
checks every hash and header before host replay and writes its generated report
to `artifacts/lora_validation/corpus/differential_report.json`.

Current corpus composition is six controlled positives, one uncontrolled
background-LoRa positive, and three confirmed no-preamble negatives. Current
host replay recovered all six exact controlled packets and the unrelated
position packet. Native evidence is classified only when a durable sidecar or
specific engineering record supports it:

| Capture ID | Ground truth / packet ID | Host | Native | Class | Native stage | Host ms | Native ms |
|---|---|---|---|:---:|---|---:|---:|
| `orciq-7c6f476e9a8b6740` | controlled / `0xda4f1809` | pass | pass | A | complete | 2999.573 | 12747 |
| `orciq-38e1d39b93c2ab9e` | no-preamble negative | fail | fail | C | preamble search | 977.024 | 12641 |
| `orciq-1d46413a2605eba5` | no-preamble negative | fail | fail | C | preamble search | 972.570 | 12640 |
| `orciq-0f4812e88b88bd75` | controlled / `0xfb7a31ca` | pass | pass | A | complete | 2115.400 | 12804 |
| `orciq-f91ff779153ca577` | controlled / `0x6b7b15cc` | pass | pass | A | complete | 2058.937 | 12806 |
| `orciq-66028e4a1e83f433` | controlled / `0xa30f7cb9` | pass | pass | A | complete | 4352.008 | 13095 |
| `orciq-072fbd6a17a2924e` | no-preamble negative | fail | fail | C | preamble search | 973.991 | 12637 |
| `orciq-582e2fe308339967` | background LoRa / `0x55ebab82` | pass | pass | A | complete | 2041.718 | 12762 |
| `orciq-d9f355473ea17c15` | controlled / `0xeca065e3` | pass | pass | A | complete | 1989.679 | 12742 |
| `orciq-d3d7b68a288edd7a` | controlled / `0xc6c8e1e5` | pass | pass (2 packets) | A | complete | 4109.583 | 12888 |

Final totals are A=7, B=0, C=3, D=0, and unknown=0. The seven Class A files are
the six controlled positives plus the unrelated background-LoRa packet in the
whip control window. The latter is not a false positive: it has a valid PHY CRC
and agrees with the host decoder, but its transmitter is not controlled. The
three Class C files are the MLA-30+ control and the two matched ambient repeats.

### On-device deterministic replay and phase isolation

The Tab5 now has a laboratory-only replay path that accepts a content-bound
ORCIQ payload over the authenticated COM17 session, verifies its SHA-256, and
decodes it directly from PSRAM. Live RTL-SDR reception is stopped during the
upload and replay because both paths own the same IQ buffer. This proves native
decoder behavior for fixed input; it does not replace live capture-to-decode
acceptance, where the radio must remain active.

Replaying Class B capture `orciq-0f4812e88b88bd75` first reproduced the
historical failure deterministically: one preamble, a valid explicit header,
zero packets, and one payload CRC failure. Three baseline runs completed in
61.683-62.076 seconds. About 43.3 seconds was spent on 28 payload-symbol
hypotheses, about 9.9 seconds on preamble search, 5.6 seconds on filtering, and
2.3 seconds on resampling. FEC, CRC, and Meshtastic parsing were not the
bottleneck.

The host and native paths selected payload sample 1,306,387 on the failing
pass and produced 118 symbols. Their raw streams matched through zero-based
index 13; 64 later values were exactly one bin high on native. Selected raw,
filtered, and resampled CU8 values matched a float32 host reproduction, and
float-preserving, quantized-linear, and polyphase host variants all produced
the same 118-symbol reference stream. Preprocessing precision and resampler
choice were therefore not retained as fixes.

The first material divergence was the FFT output. The ESP32-P4 optimized
`dsps_fft2r_fc32_arp4_` path produced peaks only on four-bin boundaries with
near-zero interpolated neighbors. For example, symbol 14 selected bin 7148
with adjacent magnitude 0 while the next competing LoRa bin was 7144. Using a
decoder-local, correctly sized twiddle table with the same optimized routine
did not change any magnitude or symbol, rejecting the shared-table-size
hypothesis. The ANSI reference FFT with the same local table produced the
expected interpolated spectrum, selected bin 7148 with substantial energy in
bins 7147 and 7149, and recovered the host's CRC-valid symbol stream.

The retained fix calls ESP-DSP's ANSI radix-2 implementation only inside the
LoRa decoder. It adds one 128 KiB PSRAM twiddle buffer and no dependency. The
rest of OrcSDR may continue using the configured optimized ESP-DSP path. This
is independently derived from captured IQ and on-device traces. ESP-DSP is
Apache-2.0 licensed; no external decoder code was copied. A current Espressif
[ESP32-P4 HWLOOP issue](https://github.com/espressif/esp-idf/issues/19025)
reports that task switches can corrupt the optimized P4 assembly FFT and names
the ANSI implementation as its workaround. That issue corroborates the
observed failure mode but was found after the local A/B result.

With the retained path, all ten captures completed in 12.637-13.095 seconds.
Every positive finished in one full pass with one clock hypothesis and one CFO
hypothesis per accepted packet. Both controlled MLA-30+ captures changed from
CRC failure at about 62 seconds to CRC-valid completion at 12.804 and 12.806
seconds. The three no-preamble captures exhausted one search pass without a
header or CRC failure. Replay-only hashes, intermediate samples, FFT bins, and
symbol lists are gated off for normal automatic/live decoding.

The synchronization structure remains deliberately small. The existing
repeated-upchirp detector, downchirp alignment, explicit-header gate, and CRC
acceptance now pass the complete captured corpus. More advanced fractional
CFO/STO and SFO estimators are documented in the EPFL
[open-source LoRa PHY paper](https://arxiv.org/abs/2002.08208) and the GNU Radio
[receiver design paper](https://events.gnuradio.org/event/24/contributions/641/attachments/192/478/paper_tapparel.pdf).
They are not implemented here because no retained capture requires them; add
them only if a new weak-signal or long-frame regression fails with a correct
FFT.

Rejected hypotheses were CFO rounding, parabolic preamble interpolation,
eight-times FFT padding, a decoder-local table on the optimized FFT, float
preservation, and replacing linear resampling with host polyphase resampling.
The replay command was also made repeatable: `RTL_STOP` now acknowledges an
already-stopped receiver, and no-preamble replays no longer wait for symbol
traces that cannot exist.

### First native impairment boundary after the FFT fix

The deterministic replay tool can apply the existing seeded AWGN model in
memory and compare native symbols with the host's CRC-valid stream. It does not
retain another IQ file. MLA-30+ capture `orciq-0f4812e88b88bd75` at -21 dB,
seed 90500, and target RMS 0.02 measured -21.002 dB. The host passed, including
with float-linear, quantized-linear, and 25/48 polyphase preprocessing. Native
found the header but failed payload CRC after 77.264 seconds and 28 payload
hypotheses.

Native differed in 8 of 118 symbols, all by +1 bin, at indices 76, 95, 104,
107, 109, 111, 112, and 114. The first error is late and the errors cluster
toward the end; selected peak-to-runner-up ratios were only 1.002-1.052. This
was the first retained evidence for a bounded recovery path. It did not justify
replacing the clean-packet preprocessing chain.

Replay-only tracing then retained the strongest *distinct symbol* from each
existing payload FFT. It covered all eight host-correct symbols. Those eight
were also the only decisions whose strongest distinct alternative was exactly
one symbol lower; the other 102 payload alternatives were one symbol higher.
A CRC-gated fallback now applies the lower alternatives together and performs
one additional FEC/CRC decode without another FFT. On a subsequent replay of
the exact seeded impairment, native decoded one encrypted packet with valid
CRC in 12.962 seconds using 480 FFTs, one CFO hypothesis, one clock hypothesis,
and one alternate-recovery attempt. Eleven payload symbols were substituted;
LoRa FEC covered the remaining header discrepancy. The prior native result was
no packet after 77.264 seconds and 3,822 FFTs.

The same clean capture still decoded with an exact 118/118 host/native symbol
match in 12.869 seconds and did not invoke alternate recovery. The MLA-30+
no-preamble control remained negative in 12.628 seconds. This establishes a
specific improvement on one seeded -21 dB boundary, not a general sensitivity
limit; broader seeded impairment and live OTA validation remain open.

Two follow-on points used the same capture and seed. At -22 dB native again
decoded a CRC-valid encrypted packet on the first hypothesis in 12.600 seconds
with one alternate-recovery attempt. At -24 dB the host reference did not find
a CRC-valid stream, while native decoded a CRC-valid encrypted packet after its
CFO retry in 50.791 seconds and 2,391 FFTs. The -24 dB result is therefore a
native robustness observation, not a host/native sensitivity comparison.

The complete ten-capture corpus was replayed again after enabling recovery.
Classification remained A=7, B=0, C=3, D=0, unknown=0. Every clean positive
used zero recovery attempts; single-packet positives used one CFO/clock
hypothesis, while the two-packet whip power capture used two. Known negatives
remained zero-preamble. Native runtimes ranged from 12.281 to 12.892 seconds.

The corpus was replayed a third time after making the one-candidate recovery
ceiling enforceable. It again classified A=7, B=0, C=3, D=0, unknown=0. All
host-positive symbols matched exactly, every clean row used zero recovery, and
the three confirmed negatives remained zero-preamble. Runtimes were 12.665 to
13.266 seconds. The whip capture with a trailing partial signal retained the
host oracle's known `Samples ended before full packet` warnings, but its
expected packet still matched exactly.

The fixed decoder allocations now reject non-PSRAM pointers and fail without an
internal-RAM fallback. A boot self-check injects a failing allocator and verifies
one PSRAM-capable call, a failed fixed-scratch allocation result, and null
scratch pointers. The production initialization caller reports
`RTL_LORA_NATIVE_INIT_FAIL stage=psram`, leaves the native decoder not ready,
and returns control to the band-enter path so the rest of boot can continue. The
weak recovery ceiling is a compile-time value of one candidate and is also
exercised by the boot self-check.

| Stage | Internal free / largest | DMA free / largest | PSRAM free / largest | Decoder PSRAM | Task stack HWM |
| --- | ---: | ---: | ---: | ---: | ---: |
| Boot, before decoder | 121,951 / 38,912 | 82,399 / 38,912 | 27,578,088 / 27,262,976 | 0 | 0 |
| After decoder init | 82,779 / 38,912 | 43,227 / 38,912 | 26,203,252 / 25,690,112 | 657,216 | 8,400 |
| Before clean replay | 76,051 / 32,768 | 36,499 / 32,768 | 10,056,784 / 9,961,472 | 657,216 | 8,140 |
| After clean replay | 76,051 / 32,768 | 36,499 / 32,768 | 2,323,532 / 2,293,760 | 8,390,464 | 568 |
| Live after replay suite | 75,555 / 32,768 | 36,003 / 32,768 | 2,223,872 / 2,162,688 | 8,390,464 | 140 |

All byte values are direct ESP-IDF heap measurements. The configured protected
internal reserve remained 40,960 bytes throughout; it is an allocator reserve,
not a promise that one 40 KiB contiguous block remains after initialization.
The 657,216-byte fixed decoder allocation consists of two 256 KiB FFT work
buffers, the 131,072-byte ANSI FFT table, and the allocator-rounded 1,856-byte
recovery workspace. The post-replay increase is the retained resampling buffer.
Both the FFT table and recovery workspace reported `PSRAM` at init, replay, and
live stages. The 140-unit stack high-water result remained nonzero with no reset,
but is a narrow observed margin to monitor during the matrix and live soak.

The representative clean replay remained exact at 118/118 symbols in 12.971
seconds with one clock/CFO hypothesis and zero recovery. The -21 dB, seed 90500,
target-RMS 0.02 vector remained CRC-valid in 12.949 seconds with exactly one
candidate tested and one recovery success. It used 480 FFTs and did not enter a
clock or CFO sweep.

A deterministic comparison matrix then replayed the MLA-30+ and indoor-whip
15 dBm captures at -21, -22, and -23 dB with the same five seeds, 90500-90504.
Only rows where the host oracle returned the expected packet ID count below:

| Capture | SNR | Host-valid rows | Native exact among host-valid |
| --- | ---: | ---: | ---: |
| MLA-30+ | -21 dB | 5 | 5 |
| MLA-30+ | -22 dB | 5 | 5 |
| MLA-30+ | -23 dB | 2 | 2 |
| Indoor 915 MHz whip | -21 dB | 5 | 4 |
| Indoor 915 MHz whip | -22 dB | 4 | 3 |
| Indoor 915 MHz whip | -23 dB | 3 | 2 |

All native passes matched the expected packet ID. Two additional MLA-30+ rows
at -23 dB produced that native packet while the host oracle failed; they remain
native-only observations and are excluded from comparison. The useful mixed
boundary was therefore present without adding -24 dB rows. All 30 native runs
finished between 12.952 and 13.032 seconds.

The three host-valid/native-fail rows were indoor-whip seed 90501 at -21 dB and
seed 90504 at -22 and -23 dB. Each found a valid header and had exactly one
wrong primary payload symbol. The correct symbol was the strongest distinct
one-bin-lower alternate in every case, with primary/alternate ratios 1.000,
1.016, and 1.033. They are Class A adjacent-bin ambiguities, not missing
candidates, synchronization/CFO failures, or header failures.

The trace also explains why the existing collective candidate failed. It
corrected the one wrong symbol but changed 11, 12, and 14 symbols respectively,
creating 10, 11, and 13 collateral changes to symbols that already matched the
host. CRC rejected each candidate. Telemetry reported one candidate tested,
zero recovery successes, and `recovery_exhausted=1`; every run stopped near 13
seconds with one clock/CFO hypothesis and 481 FFTs. This is evidence for later
selective-candidate design, not permission to expand recovery during this
phase.

Across the matrix the smallest recorded internal and DMA largest blocks were
both 32,768 bytes, decoder PSRAM peaked at 8,390,464 bytes, and decoder-task
stack high-water remained at least 556 after the latest flash/reboot. Normal
live scanning was restored at 906.875 MHz after the matrix.

During this phase firmware was built and flashed to COM17 for measurement. The
separate one-line PSRAM placement fix for the home spectrum buffer preserves the
tracked 40 KiB internal DMA reserve and restored boot with the default native
configuration. No production trigger threshold has been enabled. Normal LoRa
reception was restarted at 906.875 MHz after replay; a newly observed live OTA
packet on this image remains a separate acceptance gate.

### Live antenna observations

Three uninterrupted ten-minute intervals observed automatic LongFast reception
at 906.875 MHz. The third interval is the **antenna disturbance test with the
915 MHz whip**: the user moved the indoor whip to about twice its prior distance
from the indoor LoRa devices. Its substantially different starting noise floor
means it is a live stability observation, not a controlled distance or
sensitivity experiment.

| Interval | Approximate setup | Start noise | RF captures | Preambles | CRC valid | Zero preamble | Header / CRC failures | Recovery attempt / success / exhausted | New drops | Valid decoder runtime | Stack minimum | Resets during interval |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| MLA-30+ | Outdoors, about 50 ft from nodes | -29.1 dBFS | 3 | 2 | 2 | 1 | 0 / 0 | 0 / 0 / 0 | 0 | 3.609-9.438 s | 76 | 0 |
| Indoor 915 MHz whip | Indoors, about 10 ft from nodes | -33.9 dBFS | 8 | 5 | 3 | 4 | 0 / 2 | 2 / 0 / 2 | 2 | 17.199-17.530 s | 140 | 0 |
| Antenna disturbance, 915 MHz whip | Indoors, about twice the prior distance | -23.4 dBFS | 5 | 3 | 3 | 2 | 0 / 0 | 0 / 0 / 0 | 1 | 3.473-17.883 s | 140 | 0 |

The whip intervals saw more full four-second captures and busy-trigger drops
than the MLA interval. The first whip interval also supplied two live
CRC-failure paths: both tested exactly one recovery candidate, reported
`recovery_exhausted=1`, and stopped in 17.159 seconds or less without adding
clock/CFO hypotheses. Internal free memory remained about 75 KiB, DMA-capable
free memory about 35 KiB, both largest internal/DMA blocks 32,768 bytes, PSRAM
free about 2.22 MiB with a 2.16 MiB largest block, and decoder PSRAM 8,390,464
bytes. Uptime increased monotonically within every interval and normal live
scanning remained active afterward.

These measurements prove bounded live behavior, packet reception, and stable
memory in the observed environments. Stronger local energy, changing ambient
traffic, near-field effects, or overload are plausible explanations for the
different trigger patterns. They do not prove that distance or antenna choice
caused the differences, do not establish RF sensitivity or range, and do not
identify the ambient packets as the user's nodes.

The existing timestamps provide only a trigger-to-decode-completion proxy. For
CRC-valid captures that range was 4.491-10.728 seconds on MLA-30+, about
20.950-21.279 seconds for the first whip, and 7.222-21.631 seconds for the
disturbance interval. Full-window capture time is included. Separate CRC-valid
and UI-publication timestamps are not present, so these values are not proven
transmit-to-screen latency.

### Decoder task stack gate

`RTL_LORA_MEMORY` calls `uxTaskGetStackHighWaterMark()` for the task named
`lora_native`, created by `xTaskCreatePinnedToCore()` with a 12,288-byte stack.
The exact ESP-IDF 5.5.4 SMP implementation counts untouched stack bytes and
divides by `sizeof(StackType_t)`. The ESP32-P4 RISC-V port defines
`StackType_t` as `uint8_t`, so the reported value is bytes. It is the minimum
free stack observed since that task was created, not its instantaneous free
space.

The 76-byte result was already the historical minimum at the start of the MLA
interval and did not decline during it. A later task lifetime started at 1,268
bytes, declined to 428 after the first full whip packet path, then to 140 after
additional live paths. The antenna disturbance interval started and ended at
140 bytes. The exact operation that first produced 76 bytes was therefore not
captured, but compiler stack-use output identifies the structural cause:

| Compiled frame | Automatic stack bytes before fix |
| --- | ---: |
| `lora_native_decode_task` | 3,568 |
| `decode_capture` | 4,832 |
| `decode_capture_pass` | 1,696 |
| `decode_symbols` | 1,264 |

The task frame contained eight 180-byte packet records plus a 1,572-byte
`Stats` object. `decode_capture` also creates multiple 1,572-byte `Stats`
temporaries whose replay trace arrays exist even during normal live decoding.
Those nested automatic frames explain why the nominal 12 KiB task stack could
approach exhaustion without recursion or a broad hypothesis search.

The narrow fix keeps the task stack allocation at 12,288 bytes and moves only
the task-owned packet array and `Stats` object into the existing fixed PSRAM
allocation path. The allocator-rounded workspace is 3,072 bytes, reports
`task_workspace=PSRAM`, and is included in `decoder_psram`. Allocation still
fails closed through the existing injected PSRAM-failure self-check and never
falls back to internal RAM. The compiled task frame fell from 3,568 to 576
bytes; no stack-size increase or decoder behavior change was made.

On the flashed build, the clean capture remained an exact 118/118-symbol,
CRC-valid decode in 12.976 seconds with one clock/CFO hypothesis and zero
recovery. The representative -21 dB seed-90500 replay retained the expected
packet identity in 13.001 seconds with 480 FFTs and exactly one successful
recovery candidate. Both ended with 3,560 bytes of remaining stack. A
three-minute automatic live check then completed two full zero-preamble
captures with the same 3,560-byte minimum, stable 32,768-byte largest
internal/DMA blocks, and normal scanning restored at 906.875 MHz. Internal
free memory remained about 75 KiB and the unchanged task allocation added no
measured internal-RAM cost.

### Single-symbol recovery oracle and rejection gate

Replay-only telemetry was extended to retain the native primary and alternate
FFT magnitudes in PSRAM. An offline oracle then substituted each eligible
one-bin-lower alternate individually and ran only downstream decode, FEC, CRC,
and exact packet-identity checks. It did not add firmware candidates or radio
work.

The three original indoor-whip failures each had exactly one useful
substitution. The useful symbol ranked first by primary/alternate ratio,
absolute magnitude margin, and normalized margin: index 89 for seed 90501 at
-21 dB, and index 103 for seed 90504 at -22 and -23 dB. A temporary production
selector therefore tried exactly the lowest-ratio eligible symbol while
retaining the one-candidate ceiling.

The exact frozen 30-row matrix rejected that selector as a production
replacement. The row-level result was 19 unchanged host-valid passes, three
newly fixed host-valid rows, two newly regressed host-valid rows, four
host-invalid/inconclusive rows, and two host-invalid native-only observations.
The two regressions were MLA-30+ seed 90500 at -21 and -22 dB. The frozen
collective candidate passed both with exact packet ID 4219089354; the
single-symbol candidate failed CRC in both.

Those two regressions are Case C, not selector mistakes. Their primary symbol
sequences differed from the host reference at 12 positions. No individual
eligible substitution restored CRC or packet identity. At -21 dB the old
collective candidate corrected 11 actual errors and left index 7 for downstream
FEC. At -22 dB it corrected 11 actual errors, left index 7, and also changed the
already-correct index 107; downstream FEC/CRC still produced the exact packet.
The temporary selector instead chose index 88 at -21 dB and index 84 at -22 dB,
which could not repair a multi-symbol error pattern alone.

The three original failures remain Case A one-symbol ambiguities. Their old
collective candidates corrected the sole actual error but also changed 10, 11,
and 13 already-correct symbols, respectively, and failed CRC. This proves that
the rows require different hard-symbol recovery shapes. No ranking metric can
make a single substitution preserve the two collective-recovery passes because
the oracle found no successful individual substitution in either row.

The acceptance rule is no loss of a previously demonstrated host-valid native
pass. The single-symbol replacement is therefore rejected despite improving
the aggregate native count from 23/30 to 26/30. The detailed 30-row differential
and seven changed-row audits are preserved in
`docs/lora-recovery-selector-differential.json`. Host-invalid rows remain
unscored even when native recovery produced the expected manifest packet ID.

Following that gate, production recovery was restored to the frozen collective
lower-alternate construction while retaining the compile-time one-candidate
ceiling. The rejected selector remains only as recorded evidence; no threshold,
second candidate, symbol combination search, additional FFT pass, or broader
synchronization work was added.

Targeted post-restore hardware replay confirmed the decision boundary. MLA-30+
seed 90500 at -21 and -22 dB again produced exact packet ID 4219089354 with one
recovery attempt, one candidate, one success, and runtimes of 13.127 and 13.068
seconds. The three indoor-whip Case A rows again failed CRC with one bounded
candidate and no packet, matching the frozen collective baseline, in 13.101
seconds each. Normal live scanning was then restored at 906.875 MHz.
