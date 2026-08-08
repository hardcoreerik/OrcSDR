# OrcSDR splash variants (A/B test set)

**Selected default: D_tviz** (TheVisualizer-inspired energy cascade, seamless loop).
The canonical SD pack is a resource-optimized 30 FPS / 300-frame encode of D:

- `../OrcSDR_Splash_1280x720_60fps_10s.orsplash`
- `../OrcSDR_Splash_Poster.jpg`

(A/B/C remain available for comparison.)

Source still: TheVisualizer sample spectrum PNG.
Each pack is **600 frames · 1280×720 · 60 fps · ~10 s** → `.orsplash` for Tab5.

## Files to try on Tab5 microSD

Copy **one** `.orsplash` to the **card root** (or `/orcsdr/`) using the **canonical name** the firmware expects:

```
OrcSDR_Splash_1280x720_60fps_10s.orsplash
```

Optional poster (static fallback):

```
OrcSDR_Splash_Poster.jpg
```

### How to swap variants

| Variant | Meaning | Source file (copy → rename on SD) |
|---|---|---|
| **A_ai** | AI image→video (waveform “alive”) | `A_ai/OrcSDR_Splash_A_ai.orsplash` |
| **B_ae** | AE-style wave + turbulent displace (seamless) | `B_ae/OrcSDR_Splash_B_ae.orsplash` |
| **C_mesh** | Mesh/heightfield 3D displace (seamless) | `C_mesh/OrcSDR_Splash_C_mesh.orsplash` |
| **D_tviz** | **Selected:** TViz-inspired cascade/peak energy (seamless) | `D_tviz/OrcSDR_Splash_D_tviz.orsplash` |

PowerShell example:

```powershell
Copy-Item F:\Ai\OrcSDR\docs\splash\variants\A_ai\OrcSDR_Splash_A_ai.orsplash `
  E:\OrcSDR_Splash_1280x720_60fps_10s.orsplash
# E: = your SD drive letter
```

Also copy the matching `*_Poster.jpg` as `OrcSDR_Splash_Poster.jpg` if you want.

## Desktop preview (no Tab5)

Open `preview_60fps.mp4` (or `A_ai/master.mp4`) in each folder.

Pack the selected Tab5 asset with `--tab5-native`; the legacy filename remains
unchanged, while its frames are stored in native 720×1280 framebuffer order.

## Tools (regenerate later)

| Script | Role |
|---|---|
| `tools/splash_pack.py` | video or frame dir → `.orsplash` |
| `tools/splash_gen_alive.py` | B / C / D procedural loops from still |
| Imagine `image_to_video` | A master clip (already used) |

## Notes

- **A** may not be perfectly seamless (AI loop). B/C/D are phase-locked \(0…2\pi\).
- Device playback uses the optimized canonical 30 FPS pack; variant test packs remain 60 FPS.
- Firmware still only auto-loads the **canonical** filename above.
