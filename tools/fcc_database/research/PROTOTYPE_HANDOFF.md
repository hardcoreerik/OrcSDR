# Follow-up: coordinator contract and research probes

Coordinator contract checkpoint: 570b69baab1edaf3c1b9a24747532307613ed553, published after initial research branch creation. Read DATABASE_FORMAT, LOOKUP_API, EVIDENCE_POLICY, ACCEPTANCE_CRITERIA, AGENT_PROTOCOL, COORDINATION, ARCHITECTURE and research handoff at this SHA. Our actual branch base remains 870454241c0d58af246755a5263a8e3ead6835c2; do not infer ancestry from contract review. No merge or rebase was attempted. The coordinator explicitly permits experimental native code before wire freeze.

## Frequency index experiment (NOT national package ABI)
All integers unsigned little-endian. Header exactly 40 bytes:
- offset 0, 8 bytes magic ASCII FCCIDX01
- offset 8, u16 major=1; offset 10, u16 minor=0
- offset 12, u32 header length=40
- offset 16, u64 record count
- offset 24, u64 record start=40
- offset 32, u64 total byte length
Record stride 32 bytes: offset 0 low Hz u64, offset 8 high Hz u64 inclusive, offset 16 prefix maximum of high Hz through this row u64, offset 24 opaque fixture record ID u64. Sort ascending low; proposed deterministic secondary ordering by high and stable ID not yet enforced. Exact frequency is low=high. No range expansion.
Golden vector, one interval [1,100], ID 7, file length 72:
4643434944583031 0100 0000 28000000 0100000000000000 2800000000000000 4800000000000000
0100000000000000 6400000000000000 6400000000000000 0700000000000000
Whitespace for readability, concatenate bytes. Test uses programmatic fixture writer.
Validate lengths, version, overflow-safe multiplication, monotonic lows and exact prefix max in incremental record-budgeted passes. On query, binary search for first prefix max >= query low, scan until record low > query high, filter high >= query low. This is correct for immutable verified storage, including long blocks; each step reads <= caller budget, returns <=16 IDs, no heap allocation in reader, fixed-size buffers, no M5GFX/OS dependency. Storage is abstract read_at. Cancellation is cooperative at step boundary; abandon cursor on invalid/I/O failure or cancellation. Public cursors are trusted experimental objects, not serialized tickets. Reader assumes storage immutability and pinned lifetime. No generation/lifecycle integration. Worst case O(N) candidate scan if long block spans many unrelated entries; do not freeze this layout until compared against separate exact-frequency index and paged interval tree with subtree maximum endpoints. Full package proposal remains outstanding (strings, normalized relations, geometry, coverage, signed manifests, deterministic secondary ordering, all other indexes and extensions).

## Canada staging probe
ised_probe.py implements provisional positional CSV mapping from the official ISED SMS field dictionary, not a verified production adapter. 56/61 column assumptions require checking actual files; English header checks only first two columns and are not sufficient for arbitrary reordered schemas. Reject unexpected row width/function, invalid frequency and out-of-range coordinates; preserve TX/RX, raw frequency string, source line and IDs, raw status/emission, Unicode NFC, explicit missing site, and do not use licensee address. E7 conversion uses ties-to-even; original coordinate strings retained. Decimal context/length hardening, source hashes, full schema detection, source-country completeness, joins and deduplication remain pending. Probe prints local JSONL; not signed, not a user package, not suitable for publication. A later malformed row can leave partial stdout; do not treat stdout as successful normalized data without checking exit status.

## Reproduce and test
From repository root:
    cd tools/fcc_database/research
    python -m unittest -v
    g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g interval_probe_test.cpp -o /tmp/fcc_interval_probe
    /tmp/fcc_interval_probe
    python ised_probe.py /path/to/extracted.csv --header > /path/outside/repo/staging.jsonl
Only use --header after confirming documented English field ordering; for actual no-header data omit it. Actual source file acquisition failed, no real ISED rows have been processed.
Executed Python 3.13: 14 tests passed, including six synthetic Canada cases. Initial ISED test failed because expected longitude omitted a zero; corrected test expectation (-123.25 deg = -1232500000 E7) and reran. C++17 with ASan/UBSan: 500 deterministic randomized overlap queries matched brute force; checks read budgets, prevalidation, cancellation, corruption, truncation, version, empty and uint64 endpoints. No sanitizer diagnostic observed. Synthetic corpus is 1001 records; not national measurement or physical SD performance.
Exact tested blob IDs:
interval_probe.hpp bcbf3e3e172b4f466196ed1ffca619f24e237073
interval_probe_test.cpp 558fa9e337baf72c4f114990447ef807af0d2677
ised_probe.py e39826179713f62d0bf8ae18ee87af98deee3920
test_ised_probe.py c81f44f232048ba1702e394ad499b36c4099a677

## Remaining gates
National ULS/LMS/ICFS/ELS and Canada acquisition, actual schemas and counts, redistribution approval, normalized complete relationships, source evidence for WQFV834 and competing candidates, full deterministic package format, signing, embedded storage adapter, query features, emission parser/ranking and real-data benchmarks remain open. No full source checkpoint checkout, no national package, no authenticated real data or hardware work. Research branch only. Independent tester may reproduce these probes, but must not treat passing fixtures as country support or production approval.
