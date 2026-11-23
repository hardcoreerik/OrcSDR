"""Generate deterministic host previews through the real dashboard and rune code.

Windows: python -I tests/dead_air_runes_examples.py <output-directory>
Requires WSL g++, Pillow and the existing managed M5GFX component (bitmap font).
This is a host primitive preview, not a physical Tab5 screenshot.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)

def linux(path):
    s = str(path.resolve()).replace('\\', '/')
    return '/mnt/' + s[0].lower() + s[2:]

with tempfile.TemporaryDirectory(prefix='veil-runes-preview-') as temp:
    temp = Path(temp)
    # Preserve primitive endpoints/color and default-font scale in a private copy
    # of the recording stand-in; normal test infrastructure is unchanged.
    stub = (root / 'tests/acars_ui_stubs/M5Unified.h').read_text()
    stub = stub.replace('inline Font DejaVu18', 'inline Font Bitmap{6, 8};\ninline Font DejaVu18')
    stub = stub.replace('inline int datum', 'inline int scale = 1;\ninline int datum')
    stub = stub.replace('void setTextSize(float) {}', 'void setTextSize(float s) { host_display::scale = static_cast<int>(s); }')
    stub = stub.replace('&fonts::DejaVu18', '&fonts::Bitmap')
    stub = stub.replace('* f->advance', '* f->advance * host_display::scale')
    stub = stub.replace('y - f->height / 2, w, f->height', 'y - f->height * host_display::scale / 2, w, f->height * host_display::scale')
    stub = stub.replace('int, uint16_t c) { add(Prim::fill, x, y, w, h, c); }', 'int, uint16_t c) { add(Prim::fill, x, y, w, h, c, "round"); }')
    stub = stub.replace('int, uint16_t) { add(Prim::outline, x, y, w, h); }', 'int, uint16_t c) { add(Prim::outline, x, y, w, h, c, "round"); }')
    stub = stub.replace('int w, uint16_t) { add(Prim::line, x, y, w, 1); }', 'int w, uint16_t c) { add(Prim::line, x, y, w, 1, c); }')
    stub = stub.replace('int h, uint16_t) { add(Prim::line, x, y, 1, h); }', 'int h, uint16_t c) { add(Prim::line, x, y, 1, h, c); }')
    stub = stub.replace('int x1, int y1, uint16_t) {', 'int x1, int y1, uint16_t c) {')
    stub = stub.replace('std::abs(y1 - y0) + 1);', '''std::abs(y1 - y0) + 1, c,
        ("line:" + std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1)).c_str());''')
    (temp / 'M5Unified.h').write_text(stub)
    ui = root / 'apps/orcsdr-tab5/ui'
    sources = ['dead_air_dashboard', 'dead_air_runes', 'dead_air_veil26', 'dead_air_sweep',
               'dead_air_palette', 'dead_air_format', 'dead_air_info', 'acars_dirty_rects',
               'dead_air_model', 'dead_air_bandplan', 'dead_air_fx', 'screen_controller']
    command = ['wsl', 'g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I' + linux(temp), '-I' + linux(ui),
               linux(root / 'tests/dead_air_runes_examples.cpp'),
               linux(root / 'tests/dead_air_ui_stubs/audio_header_stub.cpp')]
    command += [linux(ui / (name + '.cpp')) for name in sources]
    command += ['-o', linux(temp / 'examples')]
    subprocess.run(command, check=True)
    encoded = subprocess.check_output(['wsl', linux(temp / 'examples')])
    assert encoded == subprocess.check_output(['wsl', linux(temp / 'examples')])
    examples = json.loads(encoded)

# Use the project's actual default bitmap font, not a system substitute. Its
# BSD license remains in managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h.
font_path = root / 'apps/orcsdr-tab5/managed_components/m5stack__m5gfx/src/lgfx/Fonts/glcdfont.h'
font_source = font_path.read_text()
(out / 'FONT_LICENSE.txt').write_text(font_source.split('*/', 1)[0] + '*/\n')
font = font_source.split('font[] PROGMEM = {', 1)[1].split('};', 1)[0]
font = re.sub(r'//[^\n]*', '', font)
font = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', font))

def rgb(value):
    return ((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31)

manifest = []
expected = [(0xDF6DDC5D, 'X', 'PRISM', 21, 'ingwaz'),
            (0xDEEC10D6, 'O', 'UMBRA', 22, 'dagaz'),
            (0xDE8C4331, 'H', 'JUNCTION', 1, 'uruz')]
for i, example in enumerate(examples, 1):
    # Independent arithmetic cross-check of the actual C++ engine output.
    seed = int.from_bytes(hashlib.sha256(('VEIL26-v1:' + example['intention']).encode()).digest()[:4], 'big')
    assert example['seed'] == seed
    assert example['freq_code'] == round(example['frequency_hz'] / 1000)
    assert example['level_code'] == round((example['level_dbfs'] + 160) * 10)
    state = ((example['seed'] * 33) ^ (example['freq_code'] * 131) ^
             (example['level_code'] * 17) ^ example['frame']) & 0xFFFFFFFF
    assert state == example['state'] and state % 24 == example['rune_index']
    assert chr(65 + state % 26) == example['letter']
    assert tuple(example[k] for k in ['state', 'letter', 'word', 'rune_index', 'rune_name']) == expected[i-1]
    image = Image.new('RGB', (1280, 720), 'black')
    draw = ImageDraw.Draw(image)
    for kind, x, y, w, h, color, text in example.pop('primitives'):
        color = rgb(color)
        if kind == 0:
            if text == 'round': draw.rounded_rectangle((x, y, x+w-1, y+h-1), radius=8, fill=color)
            else: draw.rectangle((x, y, x+w-1, y+h-1), fill=color)
        elif kind == 1:
            draw.rounded_rectangle((x, y, x+w-1, y+h-1), radius=8, outline=color)
        elif kind == 2:
            scale = h // 8
            for c in text:
                for col in range(5):
                    bits = font[ord(c) * 5 + col] if ord(c) < 256 else 0
                    for row in range(8):
                        if bits & (1 << row):
                            draw.rectangle((x+col*scale, y+row*scale,
                                            x+(col+1)*scale-1, y+(row+1)*scale-1), fill=color)
                x += 6 * scale
        elif kind == 3:
            endpoints = tuple(map(int, text[5:].split(','))) if text.startswith('line:') else (x, y, x+w-1, y+h-1)
            draw.line(endpoints, fill=color)
    # Crop to the actual overlay body; the shared header uses a host-only stub.
    image = image.crop((24, 118, 1256, 630))
    filename = f'example-{i}-{example["rune_name"]}.png'
    image.save(out / filename)
    example['seed_hex'] = f'{example["seed"]:08x}'
    example['state_hex'] = f'{example["state"]:08x}'
    example['rune_alphabet'] = 'elder-futhark-24'
    example['preview'] = filename
    example['preview_sha256'] = hashlib.sha256((out / filename).read_bytes()).hexdigest()
    example['preview_rgb_sha256'] = hashlib.sha256(image.tobytes()).hexdigest()
    manifest.append(example)
(out / 'examples.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
print('Dead Air deterministic examples PASS')
