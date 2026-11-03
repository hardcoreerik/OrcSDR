"""Record the production Home renderer's battery dispatch without ESP hardware."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


TESTS = Path(__file__).resolve().parent
APP = TESTS.parent
UI = APP / "ui"
FIXTURE = TESTS / "home_battery_recording_fixture.cpp"
STUBS = TESTS / "home_battery_ui_stubs"


def wsl_path(path: Path) -> str:
    return subprocess.check_output(["wsl.exe", "wslpath", "-a", str(path)], text=True).strip()


def main() -> int:
    if os.name == "nt":
        compiler = ["wsl.exe", "g++"]
        runner = ["wsl.exe"]
        source_args = [wsl_path(UI / "home_dashboard.cpp"), wsl_path(UI / "focus_nav.cpp"),
                       wsl_path(UI / "filter_standards.cpp"), wsl_path(FIXTURE)]
        include_args = ["-I" + wsl_path(STUBS), "-I" + wsl_path(UI)]
        binary = f"/tmp/home_battery_recording_{os.getpid()}"
    else:
        compiler = ["g++"]
        runner = []
        source_args = [str(UI / "home_dashboard.cpp"), str(UI / "focus_nav.cpp"),
                       str(UI / "filter_standards.cpp"), str(FIXTURE)]
        include_args = ["-I" + str(STUBS), "-I" + str(UI)]
        binary = f"/tmp/home_battery_recording_{os.getpid()}"

    try:
        subprocess.run(compiler + ["-std=c++17", *include_args, *source_args, "-o", binary], check=True)
        subprocess.run(runner + [binary], check=True)
    finally:
        if os.name == "nt":
            subprocess.run(["wsl.exe", "rm", "-f", binary], check=False)
        else:
            Path(binary).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
