# Axeman72 — OrcSDR handheld case V1

**Designer:** [Axeman72](https://github.com/Axeman72) — all model/design credit remains with the creator.

**Original build discussion:** [Case for OrcSDR setup — #202](https://github.com/hardcoreerik/OrcSDR/discussions/202)

**Original design submission:** [PR #212 — Handheld case V1 STLs](https://github.com/hardcoreerik/OrcSDR/pull/212) (submitted October 25, 2026).

**Status:** Community-designed **prototype V1**; proposed repository mirror, **not yet physically tested or accepted by OrcSDR**. Hardware licensing/redistribution terms are pending designer confirmation.

This enclosure puts an M5Stack Tab5 and an RTL-SDR together as a hand-held receiver and reduces cable clutter. It is a community accessory, not an official Oregon Radio Company product.

## V1 files

The proposed mirror preserves **all nine original STL files, the original assembly image and the designer's unmodified print notes** in [Case-V1/](Case-V1/).

| Part | Source file |
| --- | --- |
| Top enclosure | [Main box TOP.stl](Case-V1/Main%20box%20TOP.stl) |
| Bottom enclosure | [Main box BOTTOM.stl](Case-V1/Main%20box%20BOTTOM.stl) |
| RTL-SDR bracket 1 | [RTLSDR bracket 1.stl](Case-V1/RTLSDR%20bracket%201.stl) |
| RTL-SDR bracket 2 | [RTLSDR bracket 2.stl](Case-V1/RTLSDR%20bracket%202.stl) |
| Tab5 bracket 1 | [Tab5 bracket 1.stl](Case-V1/Tab5%20bracket%201.stl) |
| Tab5 bracket 2 | [Tab5 bracket 2.stl](Case-V1/Tab5%20bracket%202.stl) |
| Tab5 bracket 3 | [Tab5 bracket 3.stl](Case-V1/Tab5%20bracket%203.stl) |
| Tab5 bracket 4 | [Tab5 bracket 4.stl](Case-V1/Tab5%20bracket%204.stl) |
| Power button extension | [Tab5 power button extension.stl](Case-V1/Tab5%20power%20button%20extension.stl) |

**Designer instructions:** [Case-V1/README.md](Case-V1/README.md). **Numbered assembly image:** [Tab5 + RTLSDR V1](Case-V1/Tab5%2BRTLSDR%20case%20V1.png).

![Axeman72's numbered Tab5/RTL-SDR case assembly](Case-V1/Tab5%2BRTLSDR%20case%20V1.png)

## Original print settings

Axeman72's suggested prototype settings (see his original README for the source):

- Layer height: **0.2 or 0.3 mm**
- Walls/perimeters: **5**
- Top/bottom layers: **3**
- Infill: **20% gyroid**
- Supports: **none when correctly oriented**; exact bed-face orientation has **not yet been documented**.

Suggested hardware from the designer: **4 × 3×12 mm** self-tapping screws for Tab5 brackets, **4 × 2×12 mm** self-tapping screws for RTL-SDR, and optional **M3×8 mm** screws for Tab5 threaded inserts. The four Tab5 brackets differ: follow the numbered image.

**Safety/fit note:** Do not assume substitute screws will be safe simply because the plastic can be re-drilled. Check screw diameter, length, clearance and thread engagement against the Tab5, SDR and other parts before tightening. Confirm access to connectors, the power button and ventilation. This design has not been print-tested by the OrcSDR maintainers.

## Creator's V2 ideas (not V1 features)

In [Discussion #202](https://github.com/hardcoreerik/OrcSDR/discussions/202), Axeman72 described possible improvements: rotate the RTL-SDR vertically; relocate charging/power access; consider a PAM8302-class amplifier, external speaker and volume control; and explore direct knobs/buttons with the M5Dial as another possible interface. Additional power and battery ideas are **concepts**, not validated designs or instructions.

## Rights and recognition

The creator explicitly permitted use of his **pictures** in Discussion #202 and submitted the STL models for proposed inclusion in this repository. The PR does **not** state an explicit license for the standalone hardware models. OrcSDR proposes using its repository's **AGPL-3.0** terms as a default, **subject to Axeman72's confirmation**; do not treat that proposal as an already granted hardware license or as permission for commercial manufacturing. Maintain this distinction until confirmed.

Source model files are reproduced without alteration in the proposed integration; documentation and reviewer notes were prepared by OrcSDR maintainers. See [maintainer handoff notes](Case-V1/ORCSDR_INTEGRATION.md) for verification and release attribution.
