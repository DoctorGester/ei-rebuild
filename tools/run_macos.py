#!/usr/bin/env python3
"""Bounded native smoke run in a scratch game folder; preserves its logs and leaves installed data alone."""
import argparse
from pathlib import Path
import os
import re
import shutil
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path.home() / "workspace/ei")
    parser.add_argument("--exe", type=Path, default=Path(__file__).resolve().parent.parent / "build/macos/ei-rebuild.app/Contents/MacOS/ei-rebuild")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--intro", action="store_true", help="Play the startup movies too")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error("--seconds must be between 1 and 60")
    data, exe = args.data.resolve(), args.exe.resolve()
    if not (data / "Res/database.res").is_file() or not exe.is_file():
        parser.error("the game data or native executable is missing")
    runs = Path(__file__).resolve().parent.parent / "build/macos/runs"
    runs.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="smoke-", dir=runs))
    for name in ["Res", "Maps", "Movies", "Stream", "Camera"]:
        if (data / name).is_dir(): (root / name).symlink_to(data / name, target_is_directory=True)
    shutil.copytree(data / "Config", root / "Config")
    settings = (data / "ei_settings.ini").read_text() if (data / "ei_settings.ini").exists() else "[video]\nVideoResolution = 1280x800\nFullScreen = false\n"
    settings = re.sub(r"^VideoResolution\s*=.*$", "VideoResolution = 1280x800", settings, flags=re.M)
    settings = re.sub(r"^FullScreen\s*=.*$", "FullScreen = false", settings, flags=re.M)
    settings = re.sub(r"^FPSIndependentCursor\s*=.*$", "FPSIndependentCursor = false", settings, flags=re.M)
    (root / "ei_settings.ini").write_text(settings)
    if not args.intro:
        movie = root / "Config/movie.ini"
        text = movie.read_text()
        text = re.sub(r"(\[Start\]\s*\n)movies=.*", r"\1movies=", text, flags=re.I)
        movie.write_text(text)
    env = os.environ.copy()
    env.update(EI_GAME_ROOT=str(root), EI_OUT_DIR=str(root), EI_SETTINGS_PATH=str(root / "ei_settings.ini"))
    print("Scratch run: " + str(root), flush=True)
    with (root / "stdout.txt").open("w") as output:
        process = subprocess.Popen([str(exe), *(["--check"] if args.check else [])], cwd=root,
                                   env=env, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        print("Game PID: " + str(process.pid), flush=True)
        try:
            code = process.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            code = 124
            print("Smoke run reached its time limit; process stopped.", flush=True)
    print("Exit: " + str(code), flush=True)
    print((root / "stdout.txt").read_text(), end="")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
