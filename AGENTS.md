# OrcSDR development requirements

Before firmware development, builds/flashes, or hardware debugging, read
[docs/DEVELOPMENT_DIAGNOSTICS.md](docs/DEVELOPMENT_DIAGNOSTICS.md).
Apply it to every board profile: retain exact build identity, monitor without
resetting, record health before/after a hardware test, and preserve crash evidence
before replacing firmware. Keep logs, dumps and credentials private. Distinguish
host checks, firmware builds, flashing, serial evidence and physical acceptance.

Preserve existing work and protected branches. Scope changes and tests narrowly;
never merge, publish, rewrite history or erase device data without authorization.

## Licensing: no copyleft code in OrcSDR

OrcSDR is AGPL with a commercial license, so code copied or adapted from
GPL/LGPL/AGPL projects must never enter this repository, even with attribution
and even if the upstream author would not object. Examples: LakeShark, LILYGO's
`cpp_bus_driver`, `lilygo_device_driver` and T-Display-P4, and the Linux kernel.

- Reading a copyleft project to learn behaviour, ordering, timing or hardware
  facts (pins, addresses, register values, bring-up findings) is allowed.
  Copying or translating its code, comments, tables or structure is not.
  Write new code from permissive sources or from datasheets.
- Check the license of every file you reuse, not just the repository. Accept
  only MIT, BSD, Apache-2.0, ISC or equivalent. Keep the original notice, and
  record the source, commit and license in `docs/hardware/P4_REUSE.md` or the
  relevant provenance file. If a license is unclear, treat it as not reusable.
- If a task seems to need copyleft code, stop and ask the owner. Do not
  import it "separately" or behind a SPDX header; that does not fix the license.
- Never relicense or relabel copied code as OrcSDR's own.
