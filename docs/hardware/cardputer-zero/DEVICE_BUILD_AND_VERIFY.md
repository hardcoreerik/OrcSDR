# Cardputer Zero: device build and verification recipe

The Zero builds natively. This note pins the exact configuration used for device
verification so a "verify" build cannot silently be a different program from the
one users run.

## Host and paths

- Device: M5Stack CardputerZero, Raspberry Pi CM0, Debian trixie, user `hardputer`.
- Repository checkouts live in `~` (`~/OrcSDR-zero-ui`, `~/OrcSDR-zero-lora-verify`,
  and similar per-task trees).
- The CMake **source directory is `apps/orcsdr-cardputer-zero`**, not the repository
  root. The root has no `CMakeLists.txt`.

## Configure

```sh
cd ~/<tree>
cmake -S apps/orcsdr-cardputer-zero -B build/zero-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DORC_ENABLE_GUI=ON \
  -DORC_ENABLE_RTLSDR=ON \
  -DORC_ENABLE_ALSA=ON \
  -DORC_ENABLE_SDL=OFF
```

`ORC_ENABLE_RTLSDR` and `ORC_ENABLE_ALSA` each default to `OFF`. A build configured
without them still compiles, links and passes most tests, but the GUI cannot see a
receiver: it prints `LoRa: librtlsdr not detected at build time` and every radio
page stays dead. That failure looks exactly like unplugged hardware, so it is worth
checking before suspecting the dongle.

Always confirm the three lines you expect from configure:

```
-- OrcSDR: system librtlsdr enabled
-- OrcSDR: ALSA PCM enabled
-- Configuring done
```

and afterwards:

```sh
grep -E "^ORC_ENABLE_(RTLSDR|ALSA)|^CMAKE_BUILD_TYPE" build/zero-release/CMakeCache.txt
```

## Build and test

The CM0 ships with 512 MB but this OS image reports only about 350 MB usable, and
little of that is spare while the GUI is running. Stop the GUI and build serially;
a parallel build under memory pressure has pushed this board into swap and, at
least once, into an unclean reset.

```sh
ninja -C build/zero-release -j1
ctest --test-dir build/zero-release --output-on-failure
```

## Run on the panel

```sh
./build/zero-release/orcsdr_cardputer_zero_gui \
  --display fb --dashboard ft8 \
  --input /dev/input/cardputer-zero-internal \
  --audio plughw:ES8389Audio,0
```

Use `--null-audio` when only the screen matters. `--input /dev/input/eventN` also
accepts a virtual keyboard created with `/dev/uinput`, which is how scripted key
presses are driven without a human at the keyboard.

Capture whatever is on the LCD without disturbing the run:

```sh
python3 tools/capture_screen.py ~/Pictures/shot.png
```

## Keyboard capabilities that decisions depend on

The internal keyboard is the `tca8418c` controller at
`/dev/input/cardputer-zero-internal`. It reports `KEY_HOME` and the four arrow
codes, and it does **not** report `KEY_FN`. Any bindings built on an `EV_KEY`
`KEY_FN` modifier therefore cannot trigger on this hardware, however well they
compile. Dump the capability bitmap before relying on a scancode:

```sh
python3 - <<'PY'
import fcntl, os
EVIOCGBIT_KEY = (2 << 30) | (96 << 16) | (0x45 << 8) | 0x21
buf = bytearray(96)
fd = os.open("/dev/input/cardputer-zero-internal", os.O_RDONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, EVIOCGBIT_KEY, buf)
def has(code):
    return (buf[code // 8] >> (code % 8)) & 1
print("KEY_HOME", "present" if has(102) else "absent")
print("KEY_FN", "present" if has(464) else "absent")
PY
```

## Screen timeout

The vendor launcher (`APPLaunch.service`) blanks and locks the panel after
`dark_time` seconds of idle, which also hides a running OrcSDR since OrcSDR draws
to the framebuffer itself. Set it to `0` in `~/.config/cardputerzero/config.json`
to keep the panel on during bring-up, then restart the service:

```sh
systemctl --user restart APPLaunch.service
```
