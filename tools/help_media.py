#!/usr/bin/env python3
"""Capture, validate, annotate, document, and render OrcSDR help media."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import re
import secrets
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs" / "help_media" / "manifest.json"
ASSETS = ROOT / "docs" / "user-guide" / "assets" / "screenshots"
REQUIRED_PREFIX_COUNTS = {"settings.": 8, "fm.": 5, "p25.": 5, "adsb.": 5, "lora.": 3}


def load_manifest(path: Path = MANIFEST) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    defaults = data.get("defaults", {})
    data["screens"] = [{**defaults, **screen} for screen in data["screens"]]
    return data


def validate_manifest(data: dict) -> None:
    assert data["schema_version"] == 1
    assert data["capture"] == {"width": 1280, "height": 720, "live_timeout_seconds": 45}
    screens = data["screens"]
    ids = [screen["id"] for screen in screens]
    assert len(ids) == len(set(ids)), "screen IDs must be unique"
    for prefix, count in REQUIRED_PREFIX_COUNTS.items():
        assert sum(screen_id.startswith(prefix) for screen_id in ids) == count, prefix
    for band in ("am", "wx", "cb", "browse"):
        assert {f"{band}.radio", f"{band}.scope", f"{band}.capture"} <= set(ids)
    assert {"home", "nav", "overlay.volume", "overlay.frequency-keypad",
            "overlay.wifi-results", "overlay.masked-keyboard"} <= set(ids)
    for screen in screens:
        for key in ("id", "title", "dashboard", "tab", "source", "redactions",
                    "callouts", "workflow_steps", "narration", "video_timing"):
            assert key in screen, f"{screen.get('id')}: missing {key}"
        assert screen["source"] in ("live", "demo")
        assert screen["video_timing"]["seconds"] >= 2


class Tab5:
    def __init__(self, port: str, key_path: Path):
        import serial
        # USB Serial/JTAG ignores UART baud divisors; retain the project's
        # hardware-verified high-throughput transfer setting.
        self.serial = serial.Serial(None, 921600, timeout=0.25, write_timeout=5)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.key_path = key_path
        # Opening Tab5's native USB Serial/JTAG port can reset the P4. Probe
        # the command loop instead of racing the measured staged boot.
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            self.send("RTL_STATUS")
            try:
                self.wait(("RTL_SDR_STATUS",), 1)
                self.serial.reset_input_buffer()
                break
            except TimeoutError:
                time.sleep(0.25)
        else:
            self.close()
            raise TimeoutError("Tab5 command loop did not become ready")

    def close(self) -> None:
        self.serial.close()

    def send(self, line: str) -> None:
        self.serial.write((line + "\n").encode("ascii"))
        self.serial.flush()

    def wait(self, prefixes: tuple[str, ...], timeout: float = 15) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").strip()
            if any(line.startswith(prefix) for prefix in prefixes):
                return line
        raise TimeoutError(f"Timed out waiting for {prefixes}")

    def authenticate(self) -> None:
        self.key_path.parent.mkdir(parents=True, exist_ok=True)
        if self.key_path.exists():
            key = bytes.fromhex(self.key_path.read_text(encoding="ascii").strip())
        else:
            key = secrets.token_bytes(32)
            self.key_path.write_text(key.hex(), encoding="ascii")
        if len(key) != 32:
            raise ValueError("Pairing key must contain exactly 32 bytes")
        self.send(f"PAIR {key.hex()}")
        pair = self.wait(("PAIR_OK", "PAIR_LOCKED", "PAIR_INVALID"))
        if pair != "PAIR_OK":
            raise RuntimeError(f"{pair}; supply the key already paired with this Tab5")
        nonce = secrets.token_bytes(16)
        host = hmac.new(key, b"host" + nonce, hashlib.sha256).hexdigest()
        self.send(f"AUTH {nonce.hex()} {host}")
        reply = self.wait(("AUTH_OK ", "AUTH_DENIED", "AUTH_ERROR", "AUTH_INVALID"))
        expected = hmac.new(key, b"device" + nonce, hashlib.sha256).hexdigest()
        if reply != f"AUTH_OK {expected}":
            raise RuntimeError(f"Device authentication failed: {reply}")

    def doc_list(self) -> tuple[str, dict[str, set[str]]]:
        self.send("UI_DOC_LIST")
        begin = self.wait(("UI_DOC_LIST_BEGIN", "UI_DOC_ERROR"))
        if begin.startswith("UI_DOC_ERROR"):
            raise RuntimeError(begin)
        match = re.search(r'firmware="([^"]+)"', begin)
        firmware = match.group(1) if match else "unknown"
        screens: dict[str, set[str]] = {}
        while True:
            line = self.wait(("UI_DOC_SCREEN", "UI_DOC_LIST_DONE", "UI_DOC_ERROR"))
            if line == "UI_DOC_LIST_DONE":
                return firmware, screens
            if line.startswith("UI_DOC_ERROR"):
                raise RuntimeError(line)
            match = re.fullmatch(r"UI_DOC_SCREEN id=(\S+) modes=(\S+)", line)
            if not match:
                raise RuntimeError(f"Malformed screen record: {line}")
            screens[match.group(1)] = set(match.group(2).split(","))

    def wait_live(self, condition: str, timeout: int) -> bool:
        commands = {
            "fm_lock": ("RTL_SIGNAL", r"stereo_locked=1"),
            "fm_rds": ("RTL_SIGNAL", r"rds_carrier=1"),
            "p25_sync": ("RTL_P25_STATUS", r"frame_sync=1"),
            "p25_grant": ("RTL_P25_STATUS", r"grants=1"),
        }
        deadline = time.monotonic() + timeout
        if condition in commands:
            command, pattern = commands[condition]
            while time.monotonic() < deadline:
                self.send(command)
                line = self.wait(("RTL_SIGNAL_STATUS", "RTL_P25_STATUS"), 3)
                if re.search(pattern, line):
                    return True
                time.sleep(1)
            return False
        patterns = {
            "adsb_aircraft": r"RTL_ADSB_STATUS .*aircraft=[1-9]",
            "lora_packet": r"LORA_(?:MESSAGE|PACKET).*",
            "lora_position": r"LORA_(?:POSITION|GPS).*",
        }
        pattern = patterns.get(condition)
        if not pattern:
            return False
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if raw and re.search(pattern, raw.decode("utf-8", "replace")):
                return True
        return False

    def get_file(self, remote: str, destination: Path) -> str:
        self.send(f"SD_GET_BEGIN {remote.encode().hex()}")
        ready = self.wait(("SD_GET_READY", "SD_GET_ERROR"), 15)
        if ready.startswith("SD_GET_ERROR"):
            raise RuntimeError(ready)
        expected = int(re.search(r"bytes=(\d+)", ready).group(1))
        destination.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256()
        received = 0
        with destination.open("wb") as output:
            while received < expected:
                self.send("SD_GET_CHUNK")
                record = self.wait(("SD_GET_DATA", "SD_GET_ERROR"), 15)
                if record.startswith("SD_GET_ERROR"):
                    raise RuntimeError(record)
                count = int(re.search(r"bytes=(\d+)", record).group(1))
                chunk = self.serial.read(count)
                while len(chunk) < count:
                    chunk += self.serial.read(count - len(chunk))
                output.write(chunk)
                digest.update(chunk)
                received += count
        done = self.wait(("SD_GET_DONE", "SD_GET_ERROR"), 30)
        if done.startswith("SD_GET_ERROR"):
            raise RuntimeError(done)
        remote_hash = re.search(r"sha256=([0-9a-fA-F]{64})", done).group(1).lower()
        if received != expected or digest.hexdigest() != remote_hash:
            raise RuntimeError("Hash-verified SD transfer failed")
        return remote_hash


def capture(args: argparse.Namespace) -> None:
    data = load_manifest(args.manifest)
    validate_manifest(data)
    raw_dir = args.output / "raw"
    client = Tab5(args.port, args.pairing_key)
    condition_cache: dict[str, bool] = {}
    try:
        client.authenticate()
        firmware, device_screens = client.doc_list()
        requested = {screen["id"] for screen in data["screens"]}
        if requested != set(device_screens):
            raise RuntimeError(f"Manifest/device screen mismatch: manifest-only={sorted(requested-set(device_screens))}, device-only={sorted(set(device_screens)-requested)}")
        release_tokens = {args.release, args.release[:7]}
        if not any(token and token in firmware for token in release_tokens):
            raise RuntimeError(f"Connected firmware '{firmware}' does not match requested release '{args.release}'")
        for index, screen in enumerate(data["screens"], 1):
            source = screen["source"]
            condition = screen.get("live_condition")
            if source == "live" and condition:
                client.send(f"UI_DOC_SHOW {screen['id']} live")
                client.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"))
                if condition not in condition_cache:
                    condition_cache[condition] = client.wait_live(
                        condition, data["capture"]["live_timeout_seconds"])
                source = "live" if condition_cache[condition] else "demo"
            if source not in device_screens[screen["id"]]:
                source = "demo"
            client.authenticate()
            client.send(f"UI_DOC_SHOW {screen['id']} {source}")
            shown = client.wait(("UI_DOC_SHOW_DONE", "UI_DOC_ERROR"))
            if shown.startswith("UI_DOC_ERROR"):
                raise RuntimeError(shown)
            slug = screen["id"].replace(".", "-")
            client.send(f"UI_CAPTURE {slug}")
            result = client.wait(("UI_CAPTURE_DONE", "UI_CAPTURE_ERROR"), 30)
            if result.startswith("UI_CAPTURE_ERROR"):
                raise RuntimeError(result)
            remote = re.search(r'path="([^"]+)"', result).group(1)
            capture_hash = re.search(r"sha256=([0-9a-fA-F]{64})", result).group(1).lower()
            local_hash = client.get_file(remote, raw_dir / f"{slug}.bmp")
            if capture_hash != local_hash:
                raise RuntimeError(f"Capture/transfer hash mismatch for {screen['id']}")
            screen["actual_source"] = source
            screen["firmware_build"] = firmware
            screen["capture_sha256"] = capture_hash
            print(f"[{index}/{len(data['screens'])}] {screen['id']} {source} {capture_hash}")
    finally:
        try:
            client.authenticate()
            client.send("UI_DOC_EXIT")
            client.wait(("UI_DOC_EXIT_DONE", "UI_DOC_ERROR"), 10)
        except Exception as error:
            print(f"warning: UI_DOC_EXIT failed: {error}", file=sys.stderr)
        client.close()
    persisted = json.loads(args.manifest.read_text(encoding="utf-8"))
    captures = {screen["id"]: screen for screen in data["screens"]}
    for screen in persisted["screens"]:
        captured = captures[screen["id"]]
        for key in ("actual_source", "firmware_build", "capture_sha256"):
            screen[key] = captured[key]
    args.manifest.write_text(json.dumps(persisted, indent=2) + "\n", encoding="utf-8")


def annotate(args: argparse.Namespace) -> None:
    from PIL import Image, ImageDraw, ImageFont
    data = load_manifest(args.manifest)
    validate_manifest(data)
    clean_dir = ASSETS / "clean"
    annotated_dir = ASSETS / "annotated"
    clean_dir.mkdir(parents=True, exist_ok=True)
    annotated_dir.mkdir(parents=True, exist_ok=True)
    font = ImageFont.load_default(size=24)
    for screen in data["screens"]:
        slug = screen["id"].replace(".", "-")
        source = args.output / "raw" / f"{slug}.bmp"
        if not source.exists():
            raise FileNotFoundError(source)
        with Image.open(source) as opened:
            image = opened.convert("RGB")
        if image.size != (1280, 720):
            raise ValueError(f"{source}: expected 1280x720, got {image.size}")
        clean = clean_dir / f"{slug}.png"
        image.save(clean, optimize=True)
        marked = image.copy()
        draw = ImageDraw.Draw(marked)
        callouts = screen["callouts"] or [{"number": 1, "label": "Current view and status", "box": [12, 12, 1256, 112]}]
        for callout in callouts:
            x, y, width, height = callout["box"]
            draw.rounded_rectangle((x, y, x + width, y + height), radius=12,
                                   outline=(255, 215, 0), width=4)
            draw.ellipse((x - 2, y - 2, x + 42, y + 42), fill=(0, 35, 45),
                         outline=(255, 215, 0), width=3)
            draw.text((x + 20, y + 20), str(callout["number"]), anchor="mm",
                      fill="white", font=font)
        marked.save(annotated_dir / f"{slug}.png", optimize=True)
    hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
              for path in sorted(clean_dir.glob("*.png"))}
    (args.output / "png-sha256.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")


def generate_catalog(args: argparse.Namespace) -> None:
    data = load_manifest(args.manifest)
    validate_manifest(data)
    destination = ROOT / "docs" / "user-guide" / "reference" / "screen-catalog.md"
    destination.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Screen catalog", "", "This catalog is generated from the versioned capture manifest.", ""]
    for screen in data["screens"]:
        slug = screen["id"].replace(".", "-")
        lines += [f"## {screen['title']}", "", f"![{screen['title']} annotated overview](../assets/screenshots/annotated/{slug}.png)", "",
                  f"Dashboard: **{screen['dashboard']}** · Tab: **{screen['tab']}** · Capture: **{screen.get('actual_source', screen['source'])}**", "",
                  screen["narration"], ""]
        lines += [f"{callout['number']}. {callout['label']}"
                  for callout in screen["callouts"]]
        lines.append("")
    destination.write_text("\n".join(lines), encoding="utf-8")


def pronounce(text: str, dictionary: dict[str, str]) -> str:
    for source in sorted(dictionary, key=len, reverse=True):
        text = text.replace(source, dictionary[source])
    return text


def timestamp(seconds: float) -> str:
    milliseconds = int(round(seconds * 1000))
    hours, milliseconds = divmod(milliseconds, 3_600_000)
    minutes, milliseconds = divmod(milliseconds, 60_000)
    secs, milliseconds = divmod(milliseconds, 1000)
    return f"{hours:02}:{minutes:02}:{secs:02}.{milliseconds:03}"


def narrate(text: str, output: Path, dictionary: dict[str, str]) -> float:
    import soundfile as sf
    from kokoro import KPipeline
    pipeline = KPipeline(lang_code="a")
    audio = []
    for _, _, segment in pipeline(pronounce(text, dictionary), voice="am_michael", speed=0.92):
        audio.extend(segment.tolist())
    sf.write(output, audio, 24000)
    return len(audio) / 24000


def render_media(args: argparse.Namespace) -> None:
    from moviepy import AudioFileClip, CompositeVideoClip, ImageClip, concatenate_videoclips
    from PIL import Image, ImageDraw, ImageFont
    data = load_manifest(args.manifest)
    output = args.output / "video"
    audio_dir = output / "audio"
    output.mkdir(parents=True, exist_ok=True)
    audio_dir.mkdir(parents=True, exist_ok=True)
    groups: dict[str, list[dict]] = {}
    for screen in data["screens"]:
        groups.setdefault(screen["dashboard"], []).append(screen)
    all_clips = []
    full_transcript = []
    full_vtt = ["WEBVTT", ""]
    full_cue = 1
    chapter_lines = []
    full_offset = 0.0
    for dashboard, screens in groups.items():
        clips = []
        transcript = []
        vtt = ["WEBVTT", ""]
        offset = 0.0
        for index, screen in enumerate(screens, 1):
            slug = screen["id"].replace(".", "-")
            audio_path = audio_dir / f"{slug}.wav"
            duration = narrate(screen["narration"], audio_path, data["pronunciation"])
            duration = max(duration + 0.5, screen["video_timing"]["seconds"])
            base = (ImageClip(str(ASSETS / "annotated" / f"{slug}.png"))
                    .resized((1920, 1080)).with_duration(duration)
                    .resized(lambda t: 1.0 + 0.02 * min(t / max(duration, 1.0), 1.0))
                    .with_position("center"))
            box = screen["callouts"][0]["box"]
            px = int((box[0] + box[2] / 2) * 1.5)
            py = int((box[1] + box[3] / 2) * 1.5)
            pulse_path = audio_dir / f"{slug}-tap.png"
            pulse_image = Image.new("RGBA", (1920, 1080), (0, 0, 0, 0))
            pulse_draw = ImageDraw.Draw(pulse_image)
            for radius, alpha in ((42, 220), (62, 130)):
                pulse_draw.ellipse((px - radius, py - radius, px + radius, py + radius),
                                   outline=(255, 215, 0, alpha), width=6)
            pulse_image.save(pulse_path)
            pulse = (ImageClip(str(pulse_path)).with_start(0.5).with_duration(0.7))
            clip = (CompositeVideoClip([base, pulse], size=(1920, 1080))
                    .with_duration(duration).with_audio(AudioFileClip(str(audio_path))))
            clips.append(clip)
            transcript.append(screen["narration"])
            vtt += [str(index), f"{timestamp(offset)} --> {timestamp(offset + duration)}",
                    screen["narration"], ""]
            full_vtt += [str(full_cue),
                         f"{timestamp(full_offset + offset)} --> {timestamp(full_offset + offset + duration)}",
                         screen["narration"], ""]
            full_transcript.append(screen["narration"])
            full_cue += 1
            offset += duration
        assembled = concatenate_videoclips(clips, method="compose")
        safe = re.sub(r"[^a-z0-9]+", "-", dashboard.lower()).strip("-")
        if safe == "shared":
            safe = "orcsdr-overview"
        raw_video = output / f"{safe}.raw.mp4"
        final_video = output / f"{safe}.mp4"
        assembled.write_videofile(str(raw_video), fps=30, codec="libx264", audio_codec="aac",
                                  preset="medium", logger=None)
        subprocess.run([args.ffmpeg, "-y", "-i", raw_video, "-af",
                        "loudnorm=I=-16:TP=-1:LRA=11", "-c:v", "copy", "-c:a", "aac",
                        "-b:a", "192k", final_video], check=True)
        raw_video.unlink()
        (output / f"{safe}.vtt").write_text("\n".join(vtt), encoding="utf-8")
        (output / f"{safe}.txt").write_text("\n\n".join(transcript) + "\n", encoding="utf-8")
        thumb = Image.open(ASSETS / "clean" / f"{screens[0]['id'].replace('.', '-')}.png").resize((1280, 720))
        draw = ImageDraw.Draw(thumb)
        draw.rectangle((0, 565, 1280, 720), fill=(0, 0, 0))
        draw.text((64, 642), f"OrcSDR: {dashboard}", anchor="lm", fill="white",
                  font=ImageFont.load_default(size=52))
        thumb.save(output / f"{safe}-thumbnail.png")
        chapter_lines.append(f"{int(full_offset//60):02}:{int(full_offset%60):02} {dashboard}")
        full_offset += offset
        all_clips.extend(clips)
    full = concatenate_videoclips(all_clips, method="compose")
    full_raw = output / "orcsdr-full-walkthrough.raw.mp4"
    full_final = output / "orcsdr-full-walkthrough.mp4"
    full.write_videofile(str(full_raw), fps=30, codec="libx264",
                         audio_codec="aac", preset="medium", logger=None)
    subprocess.run([args.ffmpeg, "-y", "-i", full_raw, "-af",
                    "loudnorm=I=-16:TP=-1:LRA=11", "-c:v", "copy", "-c:a", "aac",
                    "-b:a", "192k", full_final], check=True)
    full_raw.unlink()
    (output / "orcsdr-full-walkthrough.vtt").write_text(
        "\n".join(full_vtt), encoding="utf-8")
    (output / "orcsdr-full-walkthrough.txt").write_text(
        "\n\n".join(full_transcript) + "\n", encoding="utf-8")
    (output / "youtube-chapters.txt").write_text("\n".join(chapter_lines) + "\n", encoding="utf-8")


def voice_sample(args: argparse.Namespace) -> None:
    data = load_manifest(args.manifest)
    args.output.mkdir(parents=True, exist_ok=True)
    text = "OrcSDR receives FM, P25, ADS-B, RTL-SDR signals, and LoRa packets at 1090 megahertz."
    narrate(text, args.output / "kokoro-am-michael-sample.wav", data["pronunciation"])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("validate", "capture", "annotate", "catalog", "voice-sample", "video"))
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "help-media")
    parser.add_argument("--port", default="COM17")
    parser.add_argument("--release", default="")
    parser.add_argument("--pairing-key", type=Path, default=ROOT / ".orclink" / "ui-doc.key")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    {"validate": lambda value: validate_manifest(load_manifest(value.manifest)),
     "capture": capture, "annotate": annotate, "catalog": generate_catalog,
     "voice-sample": voice_sample, "video": render_media}[args.command](args)
    print(f"HELP_MEDIA_{args.command.upper().replace('-', '_')}_OK")


if __name__ == "__main__":
    main()
