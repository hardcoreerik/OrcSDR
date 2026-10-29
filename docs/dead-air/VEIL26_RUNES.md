# VEIL-26 runic display rule, version 1

Elder Futhark is a reconstructed 24-sign ordering of the older futhark used
roughly 2nd to 8th centuries. Names below are commonly used reconstructed
Proto-Germanic forms, not a claim of a historical "pagan system". Shapes varied
historically; these are simplified, original line drawings, not copied font
outlines. The [Unicode Runic chart](https://www.unicode.org/charts/PDF/U16A0.pdf)
is a character reference, not an embedded font or a source of interpretive text.

## Mapping and honesty

Canonical VEIL-26 is unchanged: state, state_in, seed, recurrence, Latin letter,
word and both gematria sums retain their existing rules and golden vectors.
Runes are a display function of the unsigned 32-bit state and alphabet id:
`index = state % N`. For `elder-futhark-24`, N is 24. This uses all state bits;
mapping `letter - 1` (already reduced modulo 26) to 24 would collapse two letter
pairs and give those indices about twice the weight of the others.

Modulo reduction has a small bias even for uniformly distributed states:
`2^32 = 24 * 178956970 + 16`. Indices 0..15 occur 178956971 times each;
16..23 occur 178956970 times each. The probability difference is exactly
`1 / 4294967296` (about 2.3283064e-10), relative excess about 5.588e-9.
The recipe itself is not claimed to generate uniform or random states.
Latin has no rune index; its existing N=26 mapping has remainder 22, with
165191050 occurrences for residues 0..21 and 165191049 for 22..25.

Glyph and conventional name only. No invented meanings, divination text,
messages, detections or source properties. All runes stay inside the
SYMBOLIC / INTERPRETED panel, apart from MEASURED.

## Alphabet table (zero based)

| Index | ASCII name | Reference character |
|---:|---|---|
| 0 | fehu | U+16A0 |
| 1 | uruz | U+16A2 |
| 2 | thurisaz | U+16A6 |
| 3 | ansuz | U+16A8 |
| 4 | raido | U+16B1 |
| 5 | kenaz | U+16B2 |
| 6 | gebo | U+16B7 |
| 7 | wunjo | U+16B9 |
| 8 | hagalaz | U+16BA |
| 9 | nauthiz | U+16BE |
| 10 | isa | U+16C1 |
| 11 | jera | U+16C3 |
| 12 | eihwaz | U+16C7 |
| 13 | perthro | U+16C8 |
| 14 | algiz | U+16C9 |
| 15 | sowilo | U+16CA |
| 16 | tiwaz | U+16CF |
| 17 | berkano | U+16D2 |
| 18 | ehwaz | U+16D6 |
| 19 | mannaz | U+16D7 |
| 20 | laguz | U+16DA |
| 21 | ingwaz | U+16DC |
| 22 | dagaz | U+16DE |
| 23 | othala | U+16DF |

## Rendering and control

LATIN is the session default. ALPHABET beside INTENTION cycles LATIN and ELDER
FUTHARK. It never restarts analysis, changes seed/state, clears stored frames,
or changes the canonical sequence. The selected alphabet renders both live
and inspected event states; inspecting an old event uses its saved state,
with the current display selection. Original log records retain their selection.
The Latin letter, word and sums remain visible beside the rune and name.

The dashboard uses `setFont(nullptr)`, its existing default bitmap text font;
it does not select a Runic-capable font. No Unicode coverage is assumed.
Glyphs are const segments on a 0..8 by 0..16 grid, scaled with integer arithmetic
and an inward square stroke. All pixels stay within the requested box. Existing
canvas dirty-region comparison handles changes; no new sprite, framebuffer,
DMA buffer or internal-RAM static is introduced.

Authenticated serial: `RTL_DEAD_AIR VEIL ALPHABET LATIN|ELDER`.
`RTL_DEAD_AIR VEIL` and command replies report the alphabet id. Unsupported
names (including YOUNGER) fail without changing the selection. UI and serial
share one setter. Every session start resets the selection to LATIN.

## Records

Each `veil26` line adds `rune_alphabet`, `rune_index`, `rune_name`. For example,
state `00000007` gives `"rune_alphabet":"elder-futhark-24", "rune_index":7,
"rune_name":"wunjo"`. Latin uses `"rune_alphabet":"latin"` and JSON null
for both rune fields. `veil26_config` adds `rune_alphabet`; changing selection
queues a new config before the next record, without resetting the stream.
Readers recompute from the hex `state` and the named alphabet table alone.
These additive fields retain record version 1; canonical fields are unchanged.

## Implementation and validation sequence

1. Add failing tests for independent state vectors, records, immutable engine
   state during selection, glyph validity/uniqueness and bounded strokes.
2. Add a pure const-table module; extend record formatting and queue bounds.
3. Add thin session/UI/serial hooks; extend runic render and redraw checks.
4. Wire CI/CMake; run focused host tests, then one firmware build; measure
   binary/ELF section changes against the preserved pre-change build artifacts.
5. Review the narrow diff and commit locally. No push, merge or hardware access.

Younger Futhark and Ogham are not implemented. Another alphabet can supply a
new const table and id through the same mapping/rendering logic. No recent-rune
strip is added. Device rendering, touch and serial operation remain unverified
until explicitly authorized hardware testing; host bounds do not prove appearance.

Measured build sizes, exact commands, results and limitations are recorded in
[VEIL26_RUNES_VALIDATION.md](VEIL26_RUNES_VALIDATION.md).
