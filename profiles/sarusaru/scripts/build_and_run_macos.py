#!/usr/bin/env python3
"""Build and launch the windowed SaruSaru game on Apple Silicon."""

import argparse
import fcntl
import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys

from bootstrap_macos import BUILD, PROFILE, ROOT, run


def build(iso, jobs):
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise ValueError("This script requires macOS arm64")
    for tool in ("cmake", "ninja", "clang++"):
        if shutil.which(tool) is None:
            raise ValueError(f"Missing {tool}; install Xcode command-line tools and brew install cmake ninja")
    run(sys.executable, PROFILE / "tools/prepare_game.py", iso)

    def configure(enabled):
        run("cmake", "-S", ROOT, "-B", BUILD, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DPSPRECOMP_PROFILE=sarusaru", f"-DSARUSARU_BUILD_BOOTSTRAP={enabled}",
            "-DSARUSARU_RENDERER=ON")

    if not (PROFILE / "generated/generated_registry.cpp").is_file():
        configure("OFF")
        run("cmake", "--build", BUILD, "--parallel", jobs, "--target", "psp_recomp")
        supported = json.loads((PROFILE / "config/supported_game.json").read_text())
        run(BUILD / "psp_recomp", PROFILE / "game/BOOT.elf", "--auto",
            PROFILE / "generated", supported["load_base"], supported["unit_span"])
    configure("ON")
    run("cmake", "--build", BUILD, "--parallel", jobs, "--target", "sarusaru")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        epilog="Pass game options after --, e.g. -- --settings /path/to/settings.ini --scale 3")
    parser.add_argument("--iso", type=Path, help="Game ISO (default: saved game/input.json path)")
    parser.add_argument("--jobs", type=int, default=2, choices=range(1, 9))
    arguments = sys.argv[1:]
    separator = arguments.index("--") if "--" in arguments else len(arguments)
    args = parser.parse_args(arguments[:separator])
    game_args = arguments[separator + 1:]
    # The developer runner follows the editable profile settings. Directly
    # launching the executable still uses its normal per-user configuration.
    if "--settings" not in game_args and not os.environ.get("SARUSARU_SETTINGS"):
        game_args = ["--settings", str((PROFILE / "resources/settings.ini").resolve()), *game_args]
    try:
        iso = args.iso
        if iso is None:
            metadata = PROFILE / "game/input.json"
            if not metadata.is_file():
                raise ValueError("No saved ISO path; supply --iso /path/to/game.iso")
            iso = Path(json.loads(metadata.read_text())["iso_path"])
        iso = iso.resolve()
        if not iso.is_file():
            raise ValueError(f"ISO not found: {iso}; supply --iso /path/to/game.iso")
        progress = PROFILE / "progress"
        progress.mkdir(exist_ok=True)
        # Share the bootstrap lock: both scripts modify the same build and corpus.
        with (progress / "bootstrap.lock").open("a") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as exc:
                raise ValueError("Another SaruSaru build is already running") from exc
            build(iso, args.jobs)
        # Keep the caller's working directory so relative --settings paths work.
        if "--settings" in game_args:
            index = game_args.index("--settings")
            if index + 1 < len(game_args):
                print(f"Launching with settings: {Path(game_args[index + 1]).resolve()}", flush=True)
        elif os.environ.get("SARUSARU_SETTINGS"):
            print(f"Launching with settings: {Path(os.environ['SARUSARU_SETTINGS']).resolve()}", flush=True)
        return subprocess.call([str(BUILD / "bin/sarusaru"), *game_args,
                                str(PROFILE / "game/BOOT.elf"), str(iso)])
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"build_and_run_macos: {exc}\n")
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
