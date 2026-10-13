#!/usr/bin/env python3
"""Build, test and diagnose the initial SaruSaru AOT host on Apple Silicon."""

import argparse
import fcntl
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
BUILD = ROOT / "out/macos-arm64"


def run(*command):
    print("+ " + " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True)


def bootstrap(iso, jobs):
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise ValueError("This initial bootstrap script requires macOS arm64")
    for tool in ("cmake", "ninja", "clang++"):
        if shutil.which(tool) is None:
            raise ValueError(f"Missing {tool}; install Xcode command-line tools and brew install cmake ninja")
    supported = json.loads((PROFILE / "config/supported_game.json").read_text())
    run(sys.executable, PROFILE / "tools/prepare_game.py", iso)
    elf = PROFILE / "game/BOOT.elf"
    if hashlib.sha256(elf.read_bytes()).hexdigest() != supported["executable_sha256"]:
        raise ValueError("Prepared executable identity changed")

    def configure(enabled):
        run("cmake", "-S", ROOT, "-B", BUILD, "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DPSPRECOMP_PROFILE=sarusaru", f"-DSARUSARU_BUILD_BOOTSTRAP={enabled}",
            "-DPSPRECOMP_BUILD_TESTS=ON", "-DPSPRECOMP_BUILD_PROFILE_TESTS=ON")

    configure("OFF")
    run("cmake", "--build", BUILD, "--parallel", jobs,
        "--target", "psp_analyze", "psp_recomp", "psprecomp_tests")
    run("ctest", "--test-dir", BUILD, "--output-on-failure")
    analysis = PROFILE / "analysis"
    analysis.mkdir(exist_ok=True)
    run(BUILD / "psp_analyze", elf, analysis / "main.json", supported["load_base"])
    run(BUILD / "psp_recomp", elf, "--auto", PROFILE / "generated",
        supported["load_base"], supported["unit_span"])
    configure("ON")
    run("cmake", "--build", BUILD, "--parallel", jobs, "--target", "sarusaru_bootstrap")
    run("ctest", "--test-dir", BUILD, "--output-on-failure")

    print("Running diagnostic host with a 20-second wall-clock timeout.", flush=True)
    command = [str(BUILD / "bin/sarusaru_bootstrap"), str(elf)]
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=20)
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or b"") + (exc.stderr or b"")
        (analysis / "bootstrap.log").write_bytes(output + b"\nDiagnostic timed out after 20 seconds.\n")
        raise ValueError("Diagnostic host timed out; see profiles/sarusaru/analysis/bootstrap.log") from exc
    output = result.stdout + result.stderr
    (analysis / "bootstrap.log").write_text(output, encoding="utf-8")
    print(output, end="", flush=True)
    if result.returncode == 3:
        print("AOT bootstrap compiled and executed; PSP runtime is incomplete (diagnostic exit 3).")
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path, help="Your own supported Japanese game ISO")
    parser.add_argument("--jobs", type=int, default=2, choices=range(1, 9))
    args = parser.parse_args()
    progress = PROFILE / "progress"
    progress.mkdir(exist_ok=True)
    try:
        # Generation updates a shared corpus. Do not run two bootstraps against it.
        with (progress / "bootstrap.lock").open("a") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as exc:
                raise ValueError("Another SaruSaru bootstrap is already running") from exc
            return bootstrap(args.iso.resolve(), args.jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"bootstrap_macos: {exc}\n")


if __name__ == "__main__":
    sys.exit(main())
