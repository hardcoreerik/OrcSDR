# LilyGO TX Lab integration evidence

Integration target: `codex/p4-display-integration`, preserving the validated
display, orientation, C6 recovery, maps, Wi-Fi and POCSAG work.

Imported source: `codex/lilygo-lora-tx-ui-m5gfx-20261102` at
`29f02a4e5c6e2286bc12ea288462c2aa88b1f87f`, relative to its original
`fe75c4d98ecd4450b3bc3b5e25710efded24bd18` target. The required TX policy,
snapshot/actions, profile-only P4 adapter and base renderer prerequisites were
imported with their source commit references. The one-line board-label default
from prerequisite `53e1fa72` was retained as well.

The feature is integrated in `0cd0d6e2`. Conflicts in the map touch branch and
snapshot update path preserve the newer map controls and revision throttling;
the older map renderer, decoder and map service implementation were not copied
over the validated branch. The shared header keeps its current status bounds.

The imported Operator, Messages and Dual Radio drawing templates remain native
M5GFX. A per-dashboard coordinate adapter fits the P4 safe viewport without
changing the global 1280 x 720 default. Native safe viewports exercised are
424 x 1088 at (72,72) and 1088 x 424 at (72,72). No framebuffer or radio backend
is added by the coordinate adapter. Existing legacy RX-only five-tab and
profile-capable six-tab navigation remains covered by the host suite.

## Serial controls

With LoRa open, authenticated `RTL_LORA TX VIEW OPERATOR`,
`RTL_LORA TX VIEW MESSAGES`, and `RTL_LORA TX VIEW DUAL_RADIO` queue the same
page transition used by touch. `RTL_LORA_TX_QUEUED` acknowledges admission;
`RTL_LORA_TX_VIEW_OK page=0..2` confirms UI-owner application. Invalid names,
inactive screens and a busy request slot are rejected. There is no RF-enable
serial command. Screenshots use the canonical `SCREENSHOT` command.

## Software verification

On WSL Ubuntu, both commands completed successfully:

```
bash tools/test-lora-tx-ui.sh
ORCSDR_LORA_ASAN=1 bash tools/test-lora-tx-ui.sh
```

The retained source suite renders 960 state combinations, tests portrait and
landscape navigation, admission gates, hot-removal/reconnection, RX-only
regressions and bounded ordinary updates. Added integrated tests exercise all
three native TX pages in both LilyGO safe viewports, primitive/text bounds,
44-pixel action targets, locked actions and idle receiver-revision behavior.
Logs remain in ignored `build/lora-tx-ui/` and `build/lora-tx-ui-normal/`.
An optional bounded Grok packet review produced no verdict and was stopped
after several minutes; it is not counted as a clean external review.
The integrated test uses the retained pinned OrcMaps geometry dependency;
map data/network services are neutral host stubs. CI checks out that same
immutable OrcMaps revision instead of replacing the current production map.

Native ESP-IDF 5.5.4 compilation and COM32 application flashing completed,
including esptool hash verification. Actual device PNGs of Operator, Messages
and Dual Radio exist in both orientations in the private local evidence folder
`r16-airband-shortwave-tx`; none are private flash/device snapshots. Visual
comparison with `docs/images/dashboards/lora-overview.png` retains the OrcSDR
badge, black background, pixel font, cyan borders, green active controls and
yellow unverified status. The shared native header uses the current smaller
safe-area geometry rather than copying the older wiki header coordinates.
Initial captures exposed Airband text/control overflow and a Shortwave portrait
status overflow. Focused follow-ups were imported, with stronger recording
checks; corrected captures are retained separately in `r16b-airband-shortwave-tx`.
That pass exposed an Airband portrait warning/settings overlap. A subsequent
regression failed on intersecting status text before the fix, then passed with
line-count-driven stacking and word wrapping at the existing font size. The
final targeted recapture is retained in `r16c-airband-status`.

During the initial cross-dashboard capture, RTL-SDR continued streaming at
approximately 960 kS/s on LoRa/Airband/Shortwave and returned to approximately
2.4 MS/s on Home. Wi-Fi remained connected. A subsequent 35-second Home idle
check recorded receiver drops 0 -> 0, audio dropped blocks 0 -> 0 and audio
underruns 630 -> 630. Audio was muted: this is a bounded counter check, not
audible-FM acceptance or an explanation of earlier accumulated underruns.
Post-capture memory was about 10.9 MB PSRAM free, 87.9 KB internal free and
48.3 KB DMA-capable free. These are device heap snapshots, not a leak/stress
qualification or a measured redraw latency result.

## Open acceptance gates

- **No RF transmission is enabled or verified.** SX1262 is PROFILE ONLY. SPI
  probing, RF switch/antenna path, conducted TX, regulatory/frequency approval
  and packet reception on another receiver remain unverified.
- Messaging composition/history and the Dual Radio IQ plot remain explicitly
  unavailable in the imported implementation; the grid is not live IQ data.
- Native LilyGO RX tabs still require responsive migration. The native TX
  entry shows the three TX modes and Home/Settings rather than exposing RX
  controls whose legacy geometry does not yet fit the panel. This is not a
  claim that the whole LoRa/Mesh migration is complete.
- Human physical touch/readability and RF reception acceptance remain separate
  from framebuffer captures and injected/serial input. Tab5/Hosyond physical
  regressions, long-duration continuity and constrained-memory tests are open.
- Airband/Shortwave serial action bridges remain separate follow-up work;
  their shared UI-owner control APIs are documented in their migration reports.

These checks do not authorize a main-branch merge or release.
