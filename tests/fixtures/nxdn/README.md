# NXDN IQ regression assets

These files register **11 new downloadable, source-hash-pinned local test assets**: two longer real off-air WaveShark/SigIDWiki NXDN48/96 captures (12s and 15s) and nine DSD-neo CU8 samples (clean excerpts, synthetic vectors, weak-signal, retune). All are intended for local decoder validation. **No third-party recorded traffic is committed or rehosted.** No OrcSDR decoder result is asserted by listing an asset.

## Download and verify (from repo root)

```bash
python tests/fixtures/nxdn/fetch_assets.py --list
python tests/fixtures/nxdn/fetch_assets.py --fetch all
python tests/fixtures/nxdn/fetch_assets.py --verify
```

The downloader checks original SHA-256 (WaveShark) or original Git blob SHA-1 (DSD-neo) and exact byte lengths, leaves data in gitignored `tests/fixtures/nxdn/data/`, and writes `nxdn-assets-report.json` plus `nxdn-assets.log` in the directory where invoked. A bad hash is a hard error, never a reason to update the manifest. Requires network access; download was NOT executed in the current restricted sandbox.

WaveShark: CS16 48 kS/s after xz decompression. DSD-neo: CU8 48 kS/s; never feed CU8 bytes to an NXDN CS16 CLI without a documented deterministic conversion. Both derive from the Signal Identification Wiki source and **are not two independent original RF recording sources**. The 453 MHz label on WaveShark files is synthetic; source tuning is unknown. The upstream reports source 901/group 0/RAN 1 for NXDN48 and source 2/group 0/RAN 0 for NXDN96; OrcSDR must verify independently.

The private REA-Results `research/nxdn48-nxdn96-foundation-20261119` branch already holds three different, short GopherTrunk captures. Preserve their byte-identical source hashes and rights restrictions.

Type-C long captures are documented in issue [#170](https://github.com/TrunkRecorder/trunk-recorder-pro/issues/170); this manifest stores four submission **receipts**, NOT blob hashes. Consult the public corpus metadata at `https://trunkrecorder.pro/corpus/api/v1/submissions` to obtain actual file SHA-256 and filenames; never form download URLs from receipts. Some negative no-audio captures were deleted upstream and not proven to have been restored.

Pending: local downloads, independent SHA-256 for DSD-neo blobs, real OrcSDR replay with both rates, long Type-C capture access, and distinct Type-D IQ. Redistributions of these recordings require separate rights review.
