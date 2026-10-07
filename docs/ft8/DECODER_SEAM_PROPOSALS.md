# Decoder Seam Proposals

Status: **PROPOSAL ONLY — UI owner applies approved changes**

Branch: `codex/ft8-native-decoder-research`

Baseline UI seam: `claude/ft8-ui` at `804268c71af127490364c325a76e61383042f137`

This document records the exact additive changes requested by the decoder workstream. The decoder branch does not edit `ft8_dashboard.*`, `ft8_model.*`, `ft8_hunter.*`, OrcDial, or the existing FT8 glue while this proposal is under review.

## Why any seam change is needed

The current DecoderBackend is sufficient for FT8 alone:

```cpp
struct DecoderBackend {
  void* context = nullptr;
  bool (*begin)(void* context, uint32_t sample_rate_hz) = nullptr;
  void (*reset)(void* context) = nullptr;
  bool (*begin_slot)(void* context, uint32_t slot_epoch) = nullptr;
  bool (*offer_audio)(void* context, const int16_t* samples, size_t count) = nullptr;
  size_t (*finish_slot)(void* context, Decode* output, size_t capacity) = nullptr;
};
```

The expanded scope introduces two requirements this interface cannot represent cleanly:

1. FT8, FT4, and JS8 submodes have different slot periods and framing profiles.
2. JS8 produces both individual validated RF frames and higher-level messages assembled across multiple frames.

The existing five functions should remain valid and preserve FT8 compatibility.

## Proposal A — optional mode control on DecoderBackend

Add optional tail members only. Do not change the existing callback signatures and do not make the new pointer mandatory in `backend_valid()`.

Suggested diff:

```diff
 struct DecoderBackend {
   void* context = nullptr;
   bool (*begin)(void* context, uint32_t sample_rate_hz) = nullptr;
   void (*reset)(void* context) = nullptr;
   bool (*begin_slot)(void* context, uint32_t slot_epoch) = nullptr;
   bool (*offer_audio)(void* context, const int16_t* samples, size_t count) = nullptr;
   size_t (*finish_slot)(void* context, Decode* output, size_t capacity) = nullptr;
+
+  // Optional multi-mode extension. A backend without this callback is FT8-only.
+  bool (*set_mode)(void* context, DigitalMode mode) = nullptr;
+
+  // Optional query for the currently selected decoder profile.
+  const ModeProfile* (*mode_profile)(void* context) = nullptr;
 };
```

`backend_valid()` remains exactly compatible:

```cpp
inline bool backend_valid(const DecoderBackend& backend) {
  return backend.begin != nullptr && backend.reset != nullptr &&
         backend.begin_slot != nullptr && backend.offer_audio != nullptr &&
         backend.finish_slot != nullptr;
}
```

Compatibility rule:
- no `set_mode` callback => backend behaves as FT8;
- `set_mode` present => runtime may request FT8/FT4/JS8 profile before `begin_slot`;
- a mode change resets partial candidate/slot state but does not need to destroy persistent JS8 conversation history unless explicitly requested.

### Why not pass mode into begin_slot?

Changing `begin_slot(context, epoch)` would break every existing backend implementation and the current function-pointer aggregate. An optional tail callback is lower-risk and lets the dashboard/runtime select a mode outside the time-critical slot boundary.

### Slot epoch meaning

`slot_epoch` remains Unix UTC seconds at the start of the selected profile's slot. The backend knows the slot duration from its active ModeProfile.

For sub-second boundaries such as 7.5-second FT4, whole seconds alone cannot identify every slot uniquely. Therefore the runtime should eventually call `begin_slot` with **UTC milliseconds**, or the backend must derive the half-second phase from a separate timestamp.

This is the one current signature limitation that deserves a deliberate choice.

#### Proposal A1 — preferred timing extension

Keep the old callback, add an optional higher-resolution callback:

```diff
   bool (*begin_slot)(void* context, uint32_t slot_epoch) = nullptr;
+  bool (*begin_slot_ms)(void* context, uint64_t slot_epoch_ms) = nullptr;
```

Runtime behavior:
- if `begin_slot_ms` exists, use it;
- otherwise use legacy `begin_slot` for FT8-only backends;
- FT4/JS8 multi-mode backend requires `begin_slot_ms`.

This is preferable to redefining the meaning of the existing 32-bit value.

## Proposal B — mode tag and decode provenance

The current Decode record already has a `DecodeKind` field for CQ/QSO/free-text classification. Preserve that field.

Add a mode tag and a small flags field:

```diff
+enum class DigitalMode : uint8_t {
+  ft8,
+  ft4,
+  js8_normal,
+  js8_fast,
+  js8_40,
+  js8_slow,
+  js8_60_experimental,
+};
+
+enum DecodeFlags : uint16_t {
+  decode_flag_none          = 0,
+  decode_flag_assisted      = 1u << 0,
+  decode_flag_hash_resolved = 1u << 1,
+  decode_flag_multi_frame   = 1u << 2,
+};
+
 struct Decode {
+  DigitalMode mode = DigitalMode::ft8;
+  uint16_t flags = decode_flag_none;
   uint32_t utc_epoch = 0;
   int16_t snr_db = 0;
   int16_t dt_ms = 0;
   uint16_t audio_hz = 0;
   int16_t sync_score = 0;
   char message[48]{};
   char callsign[16]{};
   char grid[9]{};
   DecodeKind kind = DecodeKind::unknown;
 };
```

Why this is needed:
- a 20 m decode row must not silently mix FT8 and JS8;
- AP-assisted decode provenance must be representable before AP can ever be enabled;
- hash resolution is receiver-derived context, not literal over-the-air identity;
- future JS8 per-frame records can be visibly distinguished from an assembled message.

Default values preserve source compatibility for current FT8 callers.

## Proposal C — JS8 assembled messages are not Decode records

Do not overload `Decode.message[48]` for an arbitrarily long keyboard conversation.

Decoder-owned proposed type:

```cpp
enum class AssemblyState : uint8_t {
  partial,
  complete,
  expired,
};

struct Js8Message {
  uint32_t first_utc_epoch = 0;
  uint32_t last_utc_epoch = 0;
  DigitalMode mode = DigitalMode::js8_normal;
  AssemblyState state = AssemblyState::partial;
  uint16_t frame_count = 0;
  uint16_t flags = 0;
  char from[16]{};
  char to[16]{};
  char text[192]{};
};
```

The initial 192-byte bound is a proposed embedded/UI storage cap, **not a JS8 protocol maximum**. The assembler must handle overflow explicitly (truncate flag, spill/store policy, or reject); it must never overflow or silently concatenate without a bound.

### Optional backend drain

Add another optional tail callback only when the JS8 UI is ready:

```diff
+  size_t (*finish_messages)(void* context,
+                            Js8Message* output,
+                            size_t capacity) = nullptr;
```

Alternative naming: `drain_messages`. The intent is that assembled messages can become available independently of the most recent RF slot.

`finish_slot()` remains the source of individual validated frame/Decode records.

## Proposal D — Hunter evidence should remain protocol-specific

No Hunter type change is required for Phase 1 FT8.

If FT4/JS8 Hunter modes are added later, Hunter evidence names must remain truthful:
- ENERGY;
- mode-specific signature;
- valid frame/decode.

Do not generalize "FT8 SIGNATURE" into a generic protocol claim without also carrying the selected mode.

The decoder workstream does not request a Hunter change now.

## Proposal E — backend capabilities

An optional capability mask avoids probing callbacks by behavior:

```cpp
enum DecoderCapability : uint32_t {
  decoder_cap_ft8          = 1u << 0,
  decoder_cap_ft4          = 1u << 1,
  decoder_cap_js8          = 1u << 2,
  decoder_cap_assisted     = 1u << 3,
  decoder_cap_message_assembly = 1u << 4,
};

uint32_t (*capabilities)(void* context) = nullptr;
```

This can wait until the UI actually needs multi-mode feature discovery. It is not required to bind the first FT8 backend.

## Exact minimum seam change recommended now

If the UI owner wants to prepare the seam before FT4 work, approve only:

1. `DigitalMode` enum.
2. `Decode.mode` defaulting to FT8.
3. `Decode.flags` defaulting to zero.
4. optional `set_mode`.
5. optional `begin_slot_ms`.

Everything else can remain decoder-internal until JS8 Phase 4.

This gives the decoder enough forward compatibility while minimizing churn in the dashboard branch.

## Timing rationale: FT4 requires millisecond slot starts

FT4 uses 7.5-second periods. Integer Unix seconds alternate between starts ending in .0 and .5 seconds. A `uint32_t slot_epoch` cannot represent the latter exactly.

The current FT8 seam can remain supported, but a multi-mode backend needs one of:
- an absolute millisecond slot epoch (preferred);
- a slot index plus mode profile;
- a separate phase bit.

`uint64_t slot_epoch_ms` is explicit, easy to test, and matches the dashboard's existing millisecond slot-clock model.

## Mode enum ownership

Two reasonable choices:

### Option 1 — UI-owned DigitalMode

Place DigitalMode in `ft8_model.hpp` so dashboard, Decode, backend, and OrcDial all share one enum.

Pros:
- no translation layer;
- mode tag in Decode is direct.

Cons:
- decoder protocol core depends on a UI-owned header if used directly.

### Option 2 — decoder-owned Mode plus UI DigitalMode

Keep `orcsdr::ftx::Mode` in `ft8_mode.hpp`; add a UI DigitalMode only to Decode and translate at the backend boundary.

Pros:
- pure decoder core stays UI-independent;
- cleaner layering.

Cons:
- one small explicit mapping is required.

**Recommendation: Option 2.** The pure codec/sync/demod core should not include dashboard/model headers. The backend adapter is the correct place to translate.

## No changes requested to current audio input contract yet

The backend continues to accept mono signed 12 kHz PCM.

The separate question of how OrcSDR produces correct USB-style analysis PCM is documented in `AUDIO_TAP_PROPOSAL.md`. That proposal must be approved independently and must not modify this decoder seam unless measurement proves 12 kHz PCM is insufficient.

## Acceptance tests for the seam

Before a seam patch is accepted:

- existing FT8 UI/model/Hunter host tests compile unchanged or with additive expected assertions;
- an old FT8-only DecoderBackend without any new optional callbacks still satisfies `backend_valid()`;
- default-initialized Decode remains FT8 with no flags;
- 7.5-second FT4 slot boundaries round-trip exactly through `begin_slot_ms`;
- unsupported mode requests fail cleanly;
- no UI code assumes AP/hash-resolved flags are ordinary literal decodes.

## Decisions requested

1. Approve optional tail callbacks rather than changing the five existing required functions.
2. Approve `begin_slot_ms(uint64_t)` for precise FT4/JS8 timing.
3. Approve a mode tag and flags in Decode.
4. Approve AP/hash-resolution provenance flags before those features are implemented.
5. Approve a separate bounded JS8 Message record and later optional drain callback.

No seam code will be changed by the decoder workstream until these decisions are approved.
