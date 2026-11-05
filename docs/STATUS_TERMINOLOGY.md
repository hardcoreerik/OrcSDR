# OrcSDR status terminology

**Reviewed:** 2026-11-04. This is the policy for current-facing product documentation. Historical evidence and tagged release notes keep the statements and labels that were true when written.

## Two independent questions

1. **Is the feature or receiver supported by the application?** The status is **Supported**, **Available**, **Limited / In development**, **Planned** or **Not available**.
2. **How was it verified?** Evidence can be host-regression-tested, build-verified, hardware-tested, RF-validated, or supported by a bounded release/package acceptance. These are qualifications, not competing product-readiness tiers.

A receiver can be **fully supported** without every band or dashboard being experimentally calibrated. A decoder can be **available** with only specific modes validated on air. Neither statement implies every combination of receiver, antenna, band, firmware and conditions has passed a release gate.

| Word | Use when | Avoid |
|---|---|---|
| **Fully supported receiver** | The maintainer accepts that named model for integrated OrcSDR operation | Calling it experimental because a historical sensitivity test is incomplete |
| **Available** | The integrated control/decoder/dashboard is present in current source or a specifically named release | Treating source availability as proof an older firmware image contains it |
| **RF-validated / Hardware-tested** | Named physical hardware, date, signal and test result exist | Using the label as a blanket calibration/performance warranty |
| **Limited / In development** | A substantive named feature is missing or its application integration is incomplete | Broad, unexplained experimental labels over a working UI |
| **Planned / Not available** | The function is not implemented for the claimed target | Presenting unfinished future plans as shipped |
| **Experimental** | A genuinely opt-in research mode, lab-only algorithm or unqualified non-product path | A default label for functioning receivers or released dashboards |
| **Provisional** | An interpretation, inference or actual measured uncertainty (e.g. SSTV sync-only lock) | A blanket substitute for a specific unmeasured RF quantity |

## Applied examples

- **Blog V4, V3c, V4L, Nooelec NESDR SMArt V5:** fully supported receiver models. Historic receiver tests remain evidence of specific builds.
- **Nooelec XTR / E4000:** driver model implemented; **OrcSDR frequency/dashboard integration in progress**. Driver support is not proof that every OrcSDR dashboard can use the tuner's approximately 64 MHz–1.7 GHz range.
- **FT8:** available and RF-validated on four receiver models at 7.074 MHz on 2026-10-09. JS8 variants have different acceptance and must not inherit FT8 status.
- **SSTV:** available; 14 modes covered by host tests, with conducted image/storage acceptance of Robot36, PD120 and Scottie S1 across four dongles on 2026-10-26. Other modes do not inherit those physical tests.
- **Shortwave and Airband:** available dedicated experiences in current source. Complete cross-band sensitivity/calibration is a separate validation goal.
- **P25 Phase II voice:** not available; Phase II grants/sync observation does not imply voice decoding.
- **LAN console:** available **only for trusted LAN use**; no TLS/authentication must be clearly displayed.
- **Research-only Stage-2 multirate frontends:** legitimately lab-only; do not imply 3.2 MS/s production WFM audio.

## Freshness rules

- State whether the page describes **current source**, **latest published release** or a **dated test artifact**.
- The authoritative current-source status is [PROJECT_STATUS.md](../PROJECT_STATUS.md); release notes remain immutable snapshots.
- Update status pages in the same PR that makes a feature user-visible. For a new receiver, document both driver implementation and application acceptance.
- When evidence is incomplete, specify the missing condition (frequency, mode, RF source, calibration, soak) rather than calling the whole product experimental.
- Don't remove legitimate RF caveats or rename non-implemented functionality for marketing reasons.
