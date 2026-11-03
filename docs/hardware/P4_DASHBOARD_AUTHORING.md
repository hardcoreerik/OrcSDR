# Shared P4 dashboard development

One dashboard implementation must serve every supported profile. Keep board pins,
service presence and safe areas in their profile/adapters; do not branch a renderer
on a board name. Reuse M5GFX primitives, shared header and existing product styling.

## Concrete pattern: LoRa receive views

- `lora_dashboard.hpp` is the shared snapshot/action contract. Unknown values stay
  unknown; a documented radio profile is not TX qualification. Hardware RX source
  and frequency remain distinct from RTL tuning and transmitter approval.
- `lora_rx_layout.hpp` derives tabs, actions, lists, plots, camera bounds and modal
  controls from the safe viewport. The same rectangles drive drawing, touch and
  focus. Pagination replaces inaccessible rows; small portrait profiles can switch
  a node pane between list and details. Never solve a fit problem by shrinking
  below the readable 16-pixel text or 44-pixel controls.
- `lora_rx_data_views.hpp` paints node/traffic values with bounded row/detail keys.
  Unchanged ticks draw nothing; updates clear only the affected widget. Real
  encrypted-but-decoded text is distinct from uninterpreted ciphertext. Empty,
  unknown and unavailable states must tell the truth.
- `lora_dashboard.cpp` owns subview, modal and camera state. Explicit transitions
  establish layout once; receiver revision/age ticks do not clear the screen.
  Waterfall updates use a bounded 128-pixel row buffer across arbitrary widths.
  Camera rendering, overlays and touch panning share the same projection bounds.
- P4 `screen_lora.cpp` adapts services and sends actions through their existing
  validation. Worker callbacks publish state only. Named serial controls queue to
  the UI owner, report admission and an applied/rejected result. Rotation redraws
  geometry while preserving current view and service state.

For a new or migrated dashboard, define these responsibilities before editing
fixed coordinates. Migrate existing content/actions; do not copy a dashboard into
another board directory or add a second web implementation. Mark a missing service
unavailable until its actual handler exists. Add capability fields only for real
behavioral differences, rather than treating a board name as readiness.

## Required checks

Run the narrow host checks against the actual renderer, not just rectangle math:

```sh
bash tools/test-lora-rx-data.sh
bash tools/test-lora-rx-integration.sh
ORCSDR_LORA_ASAN=1 bash tools/test-lora-tx-ui.sh
bash tools/test-p4-gnss.sh
```

The LoRa tests cover representative Tab5/Hosyond and LilyGO safe viewports in
both orientations, populated/empty content, bounds/minimum fonts, pagination,
map input, five/six-tab capability changes, TX admission and bounded redraws.
`ORCSDR_ORCMAPS_ROOT` selects the reviewed OrcMaps component if it is not present
at the P4 app's managed-component path. GNSS is a separate parser-only check.
Host tests do not establish physical readability, touch or RF acceptance.

Then build the real profile, archive exact symbols/config/hash, flash with the
existing settings preserved, and capture actual native PNGs/sidecars of every
page and relevant overlay. Check populated, empty and error states. Compare static
pixels across ordinary updates; image previews alone are insufficient. Check
physical controls, all rotations and sustained receiver/audio/Wi-Fi/storage with
health/resource counters separately. See [development diagnostics](../DEVELOPMENT_DIAGNOSTICS.md)
and [rendering requirements](../UI_RENDERING_STANDARD.md).

Do not promote profile flags or installation artifacts based on renderer success.
Track source/build/flash/device/human/package gates in the acceptance ledger.
