# Follow-up: actual pinned research interval reader

Supersedes the earlier report's limitation that the research header itself had not been directly executed. Exact research commit 7c0a99d30313c9670d388fbb671ade4f1cb09a21, tools/fcc_database/research/interval_probe.hpp Git blob bcbf3e3e172b4f466196ed1ffca619f24e237073. Header reconstructed from authenticated connector retrieval and verified by git hash-object; not vendored or modified.

From scratch output directory, retrieve that exact header and benchmark.cpp/pinned_reader.cpp. Verify the header blob, then run:
    c++ -std=c++17 -O2 -Wall -Wextra -Wno-return-type -I /path/to/pinned/header /path/to/pinned_reader.cpp -o pinned_reader
    ./pinned_reader > pinned-results.csv 2> pinned-execution.log

The include-based harness renames the host benchmark main (never invoked), hence the return-type warning suppression. GCC 14.2.0, x86_64. Wire uses exact 40-byte header and 32-byte little-endian record layout. Actual research validation reads every record; actual step API called with 256-record budget. Test read_at counts and logical 4096-byte page transitions include the unaligned 40-byte header; read_at returns RAM bytes, not SD. No physical claim. Timing includes page result vector appends but excludes differential comparison; not directly comparable with earlier minimal-count benchmark timing.

500,100 full sorted ID sets matched the independent split implementation (itself brute-force checked on the small fixture). No claim all large tests had brute-force oracle. Normal 100k random exact queries: 33.049 read_at calls/query, 14.174 logical page reads/query. Long-block case: 486,985.6 read_at calls/query, 3816.92 page transitions/query, maximum 3905 cooperative steps. Confirms pathology in real pinned implementation while preserving correct candidates. Initialization validation 1,002,001 and 1,002,002 read_at calls respectively, including header.

Results are in pinned-results.csv and pinned-execution.log; all other national-data, embedded, geography, lifecycle and physical qualification limitations remain. The interval probe is correct on these tests; the recommendation is performance architecture research, not a defect claim about result correctness. No production changes.
