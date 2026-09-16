# OrcSDR LoRa validation lab

`run_suite.py` records a controlled three-view baseline:

`Meshtastic TX -> Meshtastic reference RX -> OrcSDR RX`

It does not change radio configuration. Before transmitting, it verifies that
both Meshtastic devices already use the same US LongFast primary channel and
that OrcSDR reports US LongFast with automatic capture enabled. Channel keys
are compared only in memory and are never written to evidence.

Run the host-only check:

```powershell
python tools/lora_lab/run_suite.py --self-check
```

Record a serial inventory. Optional role ports are opened only long enough to
read identity/configuration; failures are recorded instead of aborting the
inventory:

```powershell
python tools/lora_lab/run_suite.py --inventory-only `
  --tx-port COM24 --reference-port COM16 --orcsdr-port COM17
```

Run a receive-only false-trigger baseline:

```powershell
python tools/lora_lab/run_suite.py --quiet-only `
  --orcsdr-port COM17 --quiet-seconds 900
```

Run a local over-the-air baseline after identifying all three ports:

```powershell
python tools/lora_lab/run_suite.py `
  --tx-port COM24 `
  --reference-port COM16 `
  --orcsdr-port COM17 `
  --transport OTA `
  --confirm-local-ota-legal
```

Interleave deterministic no-transmit controls when uncontrolled LoRa traffic
may be present:

```powershell
python tools/lora_lab/run_suite.py `
  --tx-port COM24 --reference-port COM16 --orcsdr-port COM17 `
  --transport OTA --confirm-local-ota-legal `
  --count 10 --control-count 10 --interval-seconds 20 `
  --schedule-seed 90210
```

The saved CSV labels every slot as `tx` or `control`. OrcSDR activity in control
slots estimates background contamination; it is not silently attributed to a
controlled packet.

Use `SHIELDED_RF` or `CABLED_RF` only when the physical setup actually provides
that isolation. The harness intentionally has no switch that bypasses this
transport distinction.

Each run is saved under `artifacts/lora_validation/<timestamp>/` with raw TX,
reference RX, and OrcSDR logs plus JSON, CSV, configuration, inventory, and a
Markdown summary. OrcSDR correlation is currently a non-overlapping host-time
window; payload-level correlation requires retained IQ or an explicit decoded
packet event and is not claimed by this first harness revision.

Benchmark saved ORCIQ captures without invoking the full packet decoder:

```powershell
$captures = Get-ChildItem artifacts/lora_validation/corpus -Recurse -Filter *.orciq
python tools/lora_lab/candidate_detector.py `
  --output artifacts/lora_validation/corpus/candidate_benchmark.json `
  $captures.FullName
```

The report keeps power, configured-channel occupancy, and repeated-chirp
confidence separate. Treat no-controlled-TX captures containing unrelated LoRa
as background positives, not noise negatives.

Run the deterministic offline impairment comparison with a comma-separated SNR
range. Each impaired capture is temporary and is passed through the full host
decoder; only the JSON results are retained:

```powershell
python tools/lora_lab/candidate_detector.py `
  --snr-db "10,5,0,-5,-10,-15,-20" --seed 90210 `
  --target-rms 0.05 `
  --output artifacts/lora_validation/corpus/impairment_benchmark.json `
  <controlled-mla-capture> <controlled-whip-capture>
```

Lower `--target-rms` when stronger noise would clip the quantized CU8 samples.

Verify the checked-in corpus manifest and replay every locally available IQ
capture through the host decoder:

```powershell
python tools/lora_lab/corpus_manifest.py `
  --manifest tools/lora_lab/corpus_manifest.json `
  --corpus-root artifacts/lora_validation/corpus `
  --output artifacts/lora_validation/corpus/differential_report.json
```

The manifest is tracked metadata; raw IQ and the generated report remain local
under the ignored artifact directory.
