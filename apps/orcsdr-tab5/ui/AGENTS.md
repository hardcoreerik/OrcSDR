# OrcSDR UI rendering requirements

Read [the rendering standard](../../../docs/UI_RENDERING_STANDARD.md) before adding
or changing a screen, dashboard, modal, setup page, progress display or redraw path.

- Ordinary data, progress, timer and animation updates must not clear/repaint the
  full screen. Paint only changed widgets/regions; keep static chrome visible.
- Full-screen clears belong to deliberate entry/structural layout transitions,
  never a generic revision counter or periodic refresh. Separate layout changes
  from value-only changes. A new revision alone is not a reason to repaint.
- Draw only from the UI owner/task. Asynchronous workers publish state, never
  screen pixels. Respect screen ownership, overlays and touch coordinates.
- Keep the last valid map/image visible while a replacement is prepared. Clip
  changed regions and overlays so they cannot erase controls or attribution.
- Give button presses immediate visible feedback before slow storage/network work.
- Add a focused drawing regression for changing values/progress: verify the
  displayed value updates while full-screen-clear count stays unchanged. Include
  repeated animation ticks and screen exit/re-entry where applicable.
- Validate on explicitly authorized hardware and report physical flicker
  acceptance separately from host/syntax/build results. Respect current device
  ownership and recovery/preservation instructions.

Do not rewrite unrelated existing screens or add a full-size framebuffer merely
to satisfy this rule. Measure and bound any buffer/PSRAM/DMA cost. Record existing
gaps instead of claiming the whole application has been audited.

## Serial commands

Do not add serial commands to `process_command()` in `main.cpp`. Put each family in its own
`serial_<family>` module with a host test and a recorded route, as described in
[the serial command modules guide](../../../docs/SERIAL_COMMAND_MODULES.md).
