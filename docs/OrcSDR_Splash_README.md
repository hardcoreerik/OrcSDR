# OrcSDR Tab5 Splash Asset

## Recommended asset
`docs/splash/OrcSDR_Splash_1280x720_60fps_10s.orsplash`

The legacy `60fps` filename is retained because deployed firmware already looks
for it; the selected pack's header correctly declares 24 FPS.

**Selected look: variant 4 / D_tviz** — a seamless, phase-locked energy-wave
loop based on the TheVisualizer spectrum still. The device pack is 240 baseline
JPEG frames at **24 FPS for 10 s**. Frames are stored pre-rotated at 720×1280
for direct framebuffer playback and appear as 1280×720 landscape. The original
A/B/C/D test assets remain under `docs/splash/variants/`.

Copy to Tab5 microSD **root** (or `/orcsdr/`) as:

```text
OrcSDR_Splash_1280x720_60fps_10s.orsplash
OrcSDR_Splash_Poster.jpg          (optional static fallback)
```

### ORSPLASH v1 layout (little-endian)

32-byte header:

```c
#pragma pack(push, 1)
typedef struct {
    char     magic[8];      // "ORCSPLSH"
    uint16_t version;       // 1
    uint16_t width;         // 720 in the selected native Tab5 pack
    uint16_t height;        // 1280 in the selected native Tab5 pack
    uint16_t fps;           // 24 in the selected device pack
    uint16_t frame_count;   // 240 in the selected device pack
    uint16_t flags;         // bit0=baseline JPEG, bit1=JPEG 4:2:2
    uint32_t index_offset;  // 32
    uint32_t data_offset;   // header + frame index
    uint32_t payload_bytes; // total JPEG payload bytes
} OrcSplashHeader;

typedef struct {
    uint32_t offset;        // relative to data_offset
    uint32_t length;        // JPEG frame byte count
} OrcSplashFrame;
#pragma pack(pop)
```

The header is immediately followed by `frame_count` index entries, then the JPEG payloads.

## Playback target

- Read header and frame index once at startup.
- Keep the `.orsplash` file on the Tab5 microSD card; the asset is far larger than onboard flash.
- Read each JPEG frame by `data_offset + index[i].offset` for `index[i].length` bytes.
- Decode with the ESP32-P4 hardware JPEG decoder to RGB565.
- Copy decoded frames directly into the native framebuffer at 41,667 microsecond intervals (24 FPS).
- Preserve the status and ready-button framebuffer rectangles; redraw them only when their state changes.
- Use read-ahead/double buffering so SD reads, JPEG decode, and display scanout overlap.
- After frame 239, set the frame index back to 0 without inserting a delay.

The quality-35 D_tviz pack targets about 58 KiB per frame and roughly 1.4 MiB/s
of sustained storage bandwidth during playback.

## Tab5 firmware integration (`apps/orcsdr-tab5`)

Splash is a **loading screen**, not a pre-init gate.

| Piece | Path / note |
|---|---|
| Player | `ui/orcsdr_splash.cpp` + `ui/orcsdr_splash.hpp` |
| Boot sequence | `begin` → load Wi-Fi / RTL / NVS with status updates → `set_ready(true)` → keep looping until **OrcSDR** is tapped → `end` → home UI |
| SD paths tried | `/OrcSDR_Splash_1280x720_60fps_10s.orsplash` then `/orcsdr/…` |
| SD SPI | CS=42 SCK=43 MOSI=44 MISO=39 @ 10 MHz fallback; SD_MMC is preferred |
| Start control | **OrcSDR** button only after ready; touch ignored until then |
| Fallback | Poster `/OrcSDR_Splash_Poster.jpg` or text; same ready/button rules |
| Packer | `tools/splash_pack.py` (`D_tviz/frames` → 240-frame, 24 FPS pack) |
| Stored geometry | 720×1280, pre-rotated for direct Tab5 framebuffer presentation |
| Serial logs | `SPLASH_STATUS`, `SPLASH_READY`, `SPLASH_SD_READ`, `SPLASH_JPEG`, `SPLASH_FPS`, `SPLASH_START` |

Copy the `.orsplash` (and optional poster) onto a FAT32 microSD root (or `/orcsdr/`) before boot.

## Other generated files

- `.mjpeg`: raw concatenated baseline JPEG frames, useful if you prefer your own stream parser.
- `_index.csv`: byte offsets/sizes for the raw `.mjpeg` stream.
- `_Preview_60fps.mp4`: desktop preview only; do not use H.264 as the preferred Tab5 splash path.
- `_Poster.jpg`: first frame/poster.
