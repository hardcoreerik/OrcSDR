# OrcSDR integration and release handoff — Axeman72 Case V1

This file is **OrcSDR maintainer documentation**, not part of Axeman72's original README.

## Source and preservation

- Author and model designer: [Axeman72](https://github.com/Axeman72)
- Original submission: [hardcoreerik/OrcSDR PR #212](https://github.com/hardcoreerik/OrcSDR/pull/212), head `f5d33d1ec98eb3b7d05d2fb0eda755c85e5aa586`.
- Original project context and image permission: [Discussion #202](https://github.com/hardcoreerik/OrcSDR/discussions/202).
- **9 STLs + 1 assembly PNG + original README** copied by reference to their Git blob objects without editing or re-export.
- The original case README stays at `Case-V1/README.md`; this file and `../README.md` are independent documentation by OrcSDR.

## Licensing gate

- The author submitted the models in a PR to the project's repository, but **no explicit hardware license** is present in his supplied README.
- Project maintainer's **proposed** license is OrcSDR's repository AGPL-3.0; this is **not a claim** that the author has expressly confirmed the hardware license.
- **Before accepting/merging this draft**, obtain express confirmation of redistribution and licensing, document the answer, and adjust the notice if the creator chooses different terms. Permission for pictures was expressly given in the discussion.
- Do not assume permission for commercial enclosure production or sale from the submission alone.

## Physical-print acceptance checklist (pending)

- [ ] Confirm all nine STL files load properly in the slicer and inspect geometry, scale and manifold integrity.
- [ ] Determine the intended print-bed orientation for each STL; confirm whether supports can be omitted.
- [ ] Print a test fit and verify part orientation against the numbered assembly image.
- [ ] Check Tab5 mounting, connector and power button clearance, ventilation, and RTL-SDR mechanical stress.
- [ ] Verify 3×12 mm, 2×12 mm and optional M3×8 mm screw lengths before inserting screws near electronics.
- [ ] Document slicer/printer settings and any OrcSDR-branded remix separately; label modifications and retain original creator attribution.

## Publication and contributors

- Process the files via a separately authored OrcSDR maintainer PR. Do **not** merge the community contributor's original PR directly.
- Once the maintainer PR is **merged**, close #212 without merging it and leave a respectful link back to the integration.
- Credit `Axeman72` as **hardware design author** in the community page, integration commit/PR description and the next suitable release notes. Do not insert `Co-authored-by` trailers as a substitute for hardware credit.
- Neither this draft nor the original PR has been merged as part of this handoff.

## Proposed release-note entry (not yet released)

> **Community Hardware — Axeman72's OrcSDR handheld case V1:** Added nine printable STL parts, the original assembly image, and assembly/printing instructions for a Tab5 + RTL-SDR handheld enclosure. Thanks to [Axeman72](https://github.com/Axeman72) for the design and original [PR #212](https://github.com/hardcoreerik/OrcSDR/pull/212). Community prototype; fit and license should be checked before printing or redistribution.

This is attribution copy for an eventual release; it does **not** assert that a release has been published.
