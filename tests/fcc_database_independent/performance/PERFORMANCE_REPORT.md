# Preliminary independent FCC lookup performance experiment — 2026-11-27 UTC

Status: executable synthetic investigation, NOT national-data or ESP32-P4 qualification. No production changes or ABI freeze. Baselines: testing 570b69baab1edaf3c1b9a24747532307613ed553; research 7c0a99d30313c9670d388fbb671ade4f1cb09a21; coordinator 00205ec9e719f3aeb96ec1af7f9d644f092bfccc. Research interval_probe.hpp and handoff reviewed; combined_prefix_long reproduces its algorithm, not its actual implementation or wire reader. SDK config enables PSRAM at 200M, 256KB L2/128B lines, 40KiB reserved internal malloc memory; no spare heap assumed.

## Run

From a writable scratch directory: sh /path/to/this/directory/run.sh 100000 100000
Outputs environment.log, build.log, execution.log, results.csv and fcc_benchmark in CWD. Do not overwrite committed evidence inadvertently.
Sanitizer command: c++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer benchmark.cpp -o fcc_san ; ./fcc_san 256 1000 > sanitizer-results.csv 2> sanitizer.log
Host: Linux x86_64 AMD EPYC 9V74 virtualized, GCC 14.2.0 -O2. Release and smaller sanitizer executions exited successfully; no sanitizer diagnostic. Source blob 3018ab8ccf2a7886d3eac8a2d92eb8b17513d6f8. Raw aggregate output blob 018074cea037addcb1aa857b00dbaae461c2f0f9.

## Experiment and evidence

100,000 distinct frequencies, ten IDs per frequency, 2,000 generated interval records, total 1,002,000. Stress adds one interval [1, UINT64_MAX]. Corpus seed 42; query seed 1234+mode. Mode 0 random discrete hits; 1 discrete gaps (can overlap blocks); 2 ascending; 3 repeated 32-channel loop; 4 overlap range; 5 long-block stress. Cold resets the one modeled page between queries; warm preserves it. Storage is a vector with a one-page logical read counter, NOT file IO; baseline row-sized reads are a comparator, not SD sectors. All indexes built on host, backing rows in host RAM. Counts and ID sums agree across each workload, not proof of full-set equality on the million-row corpus. Independent brute-force sorted ID vector comparison: 11 variants x 504 queries on smaller 4,561-record corpus including full uint64 range and endpoints. Total timed queries 5,500,200 (57 rows). Latency percentiles in CSV are host CPU wall elapsed per query; no physical SD, no device timing, no CPU time or per-query trace retained. Memory is vector capacity bytes, excludes backing, stack, allocator and page buffers.

## Findings

Random exact queries: split binary 30.473 modeled reads/query; sparse 2KiB 1.158 at 391680 index bytes; 4KiB 1.079 at 260608 bytes; 8KiB 1.040 at 195072 bytes. Large pages save transactions but increase transfer volume. Dictionary 1.064 reads/query at 3275264 index bytes; gap workload has zero discrete reads and returns 9010 interval candidates across 100k queries, preserving blocks. Warm dictionary ascending 0.07813 reads/query vs cold 1.0625. Long-block combined prefix 3816.89 4KiB page reads/query vs split 1.07, matching aggregate outputs. This is a synthetic adversarial case, not an expected real distribution.

## Complexity and recommendation

Sorted discrete binary O(log N + K) comparisons and worst O(log N + K) logical row reads. Sparse directory O(N/P) entries, O(log(N/P)+log P+K) work, one starting page plus result pages in typical cases. Dictionary O(U) entries, O(log U+K) query; membership gap avoids discrete IO, must still examine intervals. Augmented interval tree O(B) memory/construction after sort, O(B) worst traversal (output and pruning dependent); rejects subtree only when maximum endpoint < query low or minimum start > query high. Combined prefix index worst O(N) even with small output. Build sort O(N log N); prototype does not time build. Practical model T≈CPU + R*t_transaction + bytes/bandwidth + hydration; physical calibration needed.

Candidate production design: separate discrete runs and interval index; sparse 4KiB starting point, compare 2/8KiB and compressed directory, put immutable optional index in bounded PSRAM, actual internal core and page buffers independently budgeted. 4KiB vector capacity is already near 256KiB before omitted costs, not a core-memory pass. Low-memory candidate 8KiB, high-memory dictionary only if real U and remaining PSRAM justify it. Avoid freezing any format; propose separate interval section with subtree maximum endpoints and sparse discrete page boundaries, stable IDs, deterministic merged ordering and bounded pagination. No claim of transmitter identity. Exact membership must never skip intervals or range searches. Negative cache keys include all filters and generations; no stale publication.

## Local qualification requirements, NOT executed

First acquire/authenticate full national source snapshots and measure actual U, B, duplicate distributions and geometry. Replace memory-backed Store with bounded read-at over shared OrcSDR storage abstraction, validate immutable wire package, implement bounded page buffer and interval cursor; do not run this million-row host program on Tab5. Compare actual research reader at exact pin. Compile using OrcSDR ESP-IDF. With owner authorization, record card/filesystem, free memory, stack, DMA, baseline and loaded IQ loss, audio underruns, USB errors, decoder timing, scan duration, UI frame timing, queue depth. Compare 2/4/8KiB cold/warm and repeated workloads, actual transaction count and transfer size, p50/p95/p99/max. Require zero induced IQ loss/underruns. Test concurrent IQ recording, cancellation, generation swap, multiple countries and hydration; no flash/device access performed.

## Unfinished assignment

No national data available in this sandbox; direct Git DNS failed, authenticated connector worked. Not a direct benchmark of pinned research code. Real geographic filtering, status, pagination/order, malformed package and lifecycle testing, hash/radix/learned indexes, probabilistic filters, budget sweep, batch/cache policy comparison, physical storage, ESP32-P4 harness and compile remain open. Current prototype allocates all synthetic backing data on host; cannot be deployed. Results establish a useful trade-off only, not complete acceptance. No production files modified.
