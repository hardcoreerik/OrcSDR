# Shortwave responsive migration

The shared Shortwave dashboard now has a viewport layout seam. Its default remains the Tab5 1280 x 720 layout; the shared M5GFX renderer also accepts the P4 safe viewport and the LilyGO portrait/landscape safe rectangles. P4 rotation calls `set_viewport` and redraws from the screen owner's geometry callback. That transition does not call the radio or audio services.

## Current renderer behavior

The five existing tabs remain Live, On Air, Hunt, Memory, and Logbook. The shared renderer retains tuning, frequency entry, step/filter/mode controls, audio controls, span and spectrum/waterfall tuning, tuner gain controls, hunt results, SD memory actions, and logbook actions. Responsive list geometry is bounded by the available rows and pages records instead of dropping them. Plot tuning uses the plot's own rectangle and the waterfall's own rectangle. The waterfall row buffer is fixed at 1,232 RGB565 pixels (2,464 bytes). Responsive trace height is capped at 224 pixels; the largest calculated canvas is 1,230 x 203 x 2 = 499,380 bytes. The legacy 1,230 x 183 canvas is 450,180 bytes. Recording-host tests assert this bound; actual PSRAM allocation, DMA transfer cost and frame timing still require device measurement.

Viewport geometry is input to the shared renderer. A viewport transition preserves selected tab, selected rows/pages, entered strings, modal, receiver settings and radio service state. The orientation callback only requests a redraw; physical rendering remains unverified.

## P4 backend capability boundary

The P4 adapter does not implement the Shortwave hunt service, audio DSP controls (clean, boost, noise reduction, notch, squelch), or SD-backed memory/logbook persistence. Those controls remain visible but disabled through explicit snapshot capabilities or unavailable storage status. Do not count the shared UI action definitions as P4 service support. The adapter currently supports tuner frequency/step/span/sound/volume, AM/USB/LSB mode selection, clarifier, gain/AGC when exposed by the attached receiver, Home, and Settings.

Current P4 limitations: hunt, audio DSP, memory save/edit/favorite/delete, reception log save/edit/delete/export, hardware orientation/touch acceptance, and the serial command bridge remain open. The shared `handle_control` API returns validated `Action` values on the UI owner; it does not draw or make serial-thread calls into M5GFX. Parent integration must route those actions through the same adapter handlers as touch.

## Verification status

Host layout and recording-renderer checks are scoped to this migration. They do not establish IDF build success, physical readability, touch edge accuracy, flicker/tearing, receiver/audio continuity, or serial parity. Keep each of those as a separate acceptance gate.
