# Digital voice: licence position of the ported components

Recorded because it affects how the CardPuter Zero application may be distributed,
and because the two halves of the DMR chain are under **different** licences.

## The DMR components — AGPL-3.0-or-later

All 17 source files under `components/orcsdr_dmr/` carry
`SPDX-License-Identifier: AGPL-3.0-or-later`. This covers D0 primitives,
`d1_protocol`, `d1_frontend` and `d1_integration`.

AGPL is strong copyleft with a network clause. Linking these components into
`orcsdr_cardputer_zero_gui` means:

- Distributing a binary that includes them obliges you to offer the **complete
  corresponding source of the combined work** under AGPL.
- Offering the application as a network service obliges you to offer that source
  to the users of the service.
- It is not compatible with shipping the same binary as closed source.

If OrcSDR is itself AGPL, this is consistent. If the Zero application is intended
to ship under another licence, one of the following is needed before release:

1. Re-licence these components — only possible if the copyright holder is you,
   which the SPDX headers alone do not establish.
2. Write an independent implementation from ETSI TS 102 361-1 (the components'
   own README says they were transcribed from the specification rather than
   copied from third-party source, so the provenance is clean; the licence is a
   separate choice made later).
3. Keep the AGPL components out of the shipped binary and treat the DMR feature
   as a separate program communicating over a process boundary.

Nothing in this repository has changed the licence; this note exists so the
decision is made deliberately rather than discovered at release.

## mbelib — ISC

`components/mbelib/COPYRIGHT` grants permission "to use, copy, modify, and/or
distribute this software for any purpose with or without fee", i.e. ISC-style and
permissive. It is **not** copyleft and imposes no source obligation.

## AMBE patents — unrelated to either licence

AMBE is patented by DVSI. mbelib implements a decoder; open-source projects have
shipped it for years, but a patent licence is a commercial question that no
software licence answers. If the product is sold, take advice; doing nothing is a
decision, not an absence of one.

## Current state

The components are vendored and build on the device, but **nothing links them into
the GUI binary yet**: `orc_zero_dmr_d1` and `orc_zero_dmr_probe` are separate
targets. The licensing question therefore has no effect on the shipped application
today, and the probe can be dropped without touching the UI.
