# FT8 OrcDial design

Status: **sandbox design contract**

Branch: `codex/ft8-rx-dashboard-sandbox`

OrcDial is an optional semantic controller for the FT8 RX dashboard. The Tab5 remains authoritative for receiver state, FT8 decoding, Hunter state, and persisted data. OrcDial never fabricates callsigns, decoder results, signal measurements, or Hunter status.

## Core rule

FT8 is a workflow dashboard, not a free-running VFO.

Rotating OrcDial must never emit a generic frequency delta while FT8 is active. It emits bounded FT8 semantic actions that the Tab5 resolves against its own band table and current item lists.

```text
OrcDial rotate / press / touch
          |
          v
 encrypted semantic action
          |
          v
 Tab5 FT8 action adapter
          |
          +--> band selection
          +--> decode / station selection
          +--> Hunter control
          +--> FT8 view navigation
          |
          v
 FT8 dashboard + receiver runtime
```

## OrcDial views

The Dial follows the same six logical views as the Tab5:

| Wire view | FT8 view | Encoder rotation | Short press |
|---:|---|---|---|
| 0 | LIVE | Previous/next conventional FT8 band | Start FAST HUNT |
| 1 | DECODES | Previous/next valid decode | Activate selected decode/details |
| 2 | MAP | Previous/next decoded locator/station | Activate selected station |
| 3 | HUNTER | Previous/next Hunter band/result | Context: start FAST, stop active hunt, or listen to best completed result |
| 4 | HEARD | Previous/next decoded callsign | Activate selected station |
| 5 | SETUP | No default rotation in Phase 1 | No destructive action |

The top touch target advances to the next FT8 view. Long button press keeps the existing OrcDial behavior and returns Home.

## Deep Hunt

DECODE HUNT is deliberately a separate explicit command. It is not hidden behind encoder acceleration or inferred from a long press.

On the HUNTER Dial view, a dedicated touch affordance starts DECODE HUNT when Hunter is idle. While Hunter is active, the same location is disabled and the physical press becomes STOP.

This avoids overloaded timing gestures and keeps FAST versus DECODE HUNT visible.

## Hunter context action

The Tab5 publishes bounded FT8 capability/state bits. The Dial uses those only to choose the label and semantic command; it does not independently advance Hunter.

Context behavior:

```text
Hunter idle        -> PRESS: FAST HUNT
Hunter active      -> PRESS: STOP
Hunter complete    -> PRESS: LISTEN BEST
```

DECODE HUNT remains the explicit on-screen touch action.

## FT8 radio-state projection

The existing OrcDial `RadioState` fields are reused without inventing new measurements:

- `frequency_hz`: current Tab5 FT8 dial frequency.
- `dashboard`: FT8 dashboard ID.
- `view`: current FT8 tab/view, 0 through 5.
- `selected`: one-based selected item for the current view when valid.
- `item_count`: bounded count for that view.
- `capabilities`: FT8 status bits defined below.
- `signal_dbm`: not used for FT8 unless OrcSDR has a real calibrated dBm measurement.
- `signal_valid`: false when no calibrated signal metric exists.

For LIVE/HUNTER, `selected` is the selected FT8 band. For DECODES/MAP/HEARD it is the selected row/station. The Dial shows `--` rather than guessing when the Tab5 has no valid selected item.

## FT8 capability bits

Phase-1 FT8 Dial status uses these dashboard-specific capability bits:

| Bit | Meaning |
|---:|---|
| 0 | FT8 decoder backend bound/available |
| 1 | UTC slot clock valid |
| 2 | Hunter supported |
| 3 | Hunter currently active |
| 4 | Hunter completed with a best-band result |
| 5 | Current Hunter mode is DECODE HUNT |

Unused bits remain zero.

These bits describe state only. They are not evidence that a valid FT8 message was decoded.

## Semantic actions

FT8 adds semantic actions to the existing encrypted OrcDial action packet:

- `ft8_band`: signed previous/next band selection.
- `ft8_item`: signed previous/next item in DECODES/MAP/HEARD.
- `ft8_hunter`: explicit Hunter command.

Hunter command values:

1. start FAST HUNT
2. start DECODE HUNT
3. stop current hunt
4. listen to best completed band

`activate` remains the context-selection action for decoded items.

## 240 x 240 screen

The FT8 OrcDial screen is code-drawn with M5GFX and must remain useful with no network enrichment.

### LIVE

- FT8 RX title.
- LIVE view label.
- current conventional band (for example `20m`);
- current dial frequency (for example `14.074 MHz`);
- decoder and UTC readiness indicators;
- `TURN: BAND`;
- `PRESS: HUNT`;
- `TOP: NEXT VIEW`.

### HUNTER

- HUNTER title/state;
- selected/current band;
- frequency;
- one truthful state label: IDLE / RUNNING / COMPLETE;
- `TURN: BAND`;
- context press label: HUNT / STOP / LISTEN BEST;
- visible DEEP HUNT touch affordance only when it can be started.

### DECODES / MAP / HEARD

The packet does not contain callsign strings, so the Dial does not invent them. It shows:

- view name;
- selected item index and real item count;
- current FT8 dial frequency;
- `TURN: SELECT`;
- `PRESS: OPEN`.

Later protocol revisions may add a bounded text label if there is a demonstrated need.

### SETUP

Shows only bounded state that is already available: decoder READY/--, UTC LOCK/--, Hunter READY/--. No setting is editable from the encoder in Phase 1.

## Visual language

The FT8 Dial artwork uses eight narrow vertical tone bars and three Costas-sync blocks as a protocol motif. They are decorative protocol iconography, not a live spectrum.

The outer ring continues to indicate OrcDial link state. Green/cyan/amber meanings follow the existing OrcDial/OrcSDR visual language.

## Acceptance rules

- No FT8 rotation produces `ActionKind::tune`.
- FT8 cannot be opened through the wire until the Tab5 dashboard registry explicitly registers it.
- Invalid/missing item counts render `--`, never synthetic values.
- Hunter labels come only from Tab5-published state.
- FAST and DECODE HUNT are separate explicit actions.
- Physical short press never transmits.
- Long press continues to return Home.
- Existing dashboard control mappings remain unchanged.


## Sandbox validation - 2026-10-07

The semantic controller tests passed in GitHub Actions in both optimized and AddressSanitizer/UndefinedBehaviorSanitizer builds.

A full PlatformIO `dial` firmware build also passed with the FT8 screen, controller additions, and touch routing compiled against the pinned OrcDial M5Stack dependencies. The validation build reported 48,948 bytes RAM used of 327,680 bytes (14.9%) and 1,093,857 bytes flash used of 3,342,336 bytes (32.7%).

This proves build compatibility only. It does not prove the physical Tab5-to-OrcDial transport or FT8 runtime actions until the Tab5 FT8 dashboard is registered and those semantics are routed on hardware.
