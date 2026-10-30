#!/usr/bin/env python3
"""Run the native game with isolated writable data: timed smoke checks or persistent normal play."""
import argparse
from pathlib import Path
import os
import re
import shutil
import signal
import subprocess
import tempfile


def initial_settings(data):
    source = data / "ei_settings.ini"
    settings = source.read_text() if source.exists() else "[video]\n[game]\n"
    for section, key, value in [("video", "VideoResolution", "1280x800"),
                                ("video", "FullScreen", "false"),
                                ("video", "FPSIndependentCursor", "false")]:
        pattern = r"^" + key + r"\s*=.*$"
        if re.search(pattern, settings, flags=re.M | re.I):
            settings = re.sub(pattern, key + " = " + value, settings, flags=re.M | re.I)
        else:
            header = r"^\[" + section + r"\][^\n]*(?:\n|$)"
            line = key + " = " + value + "\n"
            if re.search(header, settings, flags=re.M | re.I):
                settings = re.sub(header, lambda match: match[0].rstrip() + "\n" + line,
                                  settings, count=1, flags=re.M | re.I)
            else:
                settings += "\n[" + section + "]\n" + line
    return settings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path.home() / "workspace/ei")
    parser.add_argument("--exe", type=Path, default=Path(__file__).resolve().parent.parent / "build/macos/ei-rebuild.app/Contents/MacOS/ei-rebuild")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--play", action="store_true", help="Play until you quit, keeping saves/settings in run/macos")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--intro", action="store_true", help="Play the startup movies too")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error("--seconds must be between 1 and 60")
    data, exe = args.data.resolve(), args.exe.resolve()
    if not (data / "Res/database.res").is_file() or not exe.is_file():
        parser.error("the game data or native executable is missing")
    repo = Path(__file__).resolve().parent.parent
    if args.play:
        root = repo / "run/macos"
        root.mkdir(parents=True, exist_ok=True)
    else:
        runs = repo / "build/macos/runs"
        runs.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix="smoke-", dir=runs))
    for name in ["Res", "Maps", "Movies", "Stream", "Camera"]:
        link = root / name
        if link.is_symlink() and link.resolve() != data / name:
            parser.error(str(link) + " already uses a different game-data folder")
        if (data / name).is_dir() and not link.exists(): link.symlink_to(data / name, target_is_directory=True)
    if not (root / "Config").exists(): shutil.copytree(data / "Config", root / "Config")
    settings = root / "ei_settings.ini"
    if not settings.exists(): settings.write_text(initial_settings(data))
    movie = root / "Config/movie.ini"
    text = movie.read_text()
    original = re.search(r"\[Start\]\s*\nmovies=(.*)", (data / "Config/movie.ini").read_text(), flags=re.I)
    movies = original[1] if args.intro and original else ""
    text = re.sub(r"(\[Start\]\s*\n)movies=.*", lambda match: match[1] + "movies=" + movies, text, flags=re.I)
    movie.write_text(text)
    env = os.environ.copy()
    env.update(EI_GAME_ROOT=str(root), EI_OUT_DIR=str(root), EI_SETTINGS_PATH=str(root / "ei_settings.ini"))
    print(("Play folder: " if args.play else "Scratch run: ") + str(root), flush=True)
    with (root / "stdout.txt").open("a" if args.play else "w") as output:
        process = subprocess.Popen([str(exe), *(["--check"] if args.check else [])], cwd=root,
                                   env=env, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        print("Game PID: " + str(process.pid), flush=True)
        try:
            code = process.wait(timeout=None if args.play else args.seconds)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            code = 124
            print("Smoke run reached its time limit; process stopped.", flush=True)
        except KeyboardInterrupt:
            os.killpg(process.pid, signal.SIGTERM)
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            code = 130
    print("Exit: " + str(code), flush=True)
    if not args.play: print((root / "stdout.txt").read_text(), end="")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
