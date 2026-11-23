# Test results (real runs; logs in logs/)

## G1 UI host failure
Before (log g1_before_release.log): link error
`undefined reference to orcsdr::screens::owns(orcsdr::screens::Id)` in dv_dashboard_surface_contract.
Cause: the test defined `g_owns` but never the `owns()` symbol the real Surface calls.
Fix (commit c2ed0406): test double `owns(Id id){return id==Id::digital_voice && g_owns;}`.
Ownership checks in Surface are untouched; the test still asserts they gate draw/update/touch.

## Host suites on merged tree 866a380d (WSL, run_host_suites.sh)
| Suite | Release | ASan/UBSan |
|---|---|---|
| Digital Voice UI (2 tests) | PASS | PASS |
| components/orcsdr_dmr D0 (3) | PASS | PASS |
| dmr d1_integration (2) | PASS | PASS |
| tests/dmr_protocol_conformance (7) | PASS | PASS |
| orcsdr_digital_voice_rx D-STAR/YSF (1) | PASS | PASS |
| orcsdr_nxdn (1) | PASS | PASS |
| orcsdr_radio_discovery (2) | PASS | PASS |

NOT run yet: pinned real-IQ replay gates (need fixture downloads), Python replay-acceptance tests,
existing P25 core host tests, any firmware self-check.
Sanitizer note: UI/DMR use their own sanitizer options; others use -fsanitize=address,undefined via CXX flags.
