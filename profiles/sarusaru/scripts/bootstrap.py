#!/usr/bin/env python3
"""Build, test and diagnose the SaruSaru AOT host on macOS arm64, Linux or Windows."""

import argparse
import contextlib
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]

# The hosts the port targets, by (system, machine) as Python reports them.
HOSTS = {
    ("Darwin", "arm64"): "macos-arm64",
    ("Linux", "x86_64"): "linux-x64",
    ("Linux", "aarch64"): "linux-arm64",
    ("Linux", "arm64"): "linux-arm64",
    ("Windows", "AMD64"): "windows-x64",
    ("Windows", "x86_64"): "windows-x64",
}


def host_name(system=None, machine=None):
    system = system or platform.system()
    machine = machine or platform.machine()
    try:
        return HOSTS[(system, machine)]
    except KeyError:
        raise ValueError(f"Unsupported host {system} {machine}; supported: "
                         + ", ".join(sorted(set(HOSTS.values())))) from None


HOST = host_name() if (platform.system(), platform.machine()) in HOSTS else None
BUILD = ROOT / "out" / (HOST or "unsupported")


def executable(path):
    """The built program at <path>, with .exe on Windows."""
    return Path(path).with_suffix(".exe") if platform.system() == "Windows" else Path(path)


def run(*command):
    print("+ " + " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True)


def check_toolchain():
    """Stops early, with what to install, when a build tool is missing."""
    host = host_name()
    if host == "macos-arm64":
        compiler, hint = "clang++", "install Xcode command-line tools and brew install cmake ninja"
    elif host.startswith("linux"):
        compiler = "c++"
        hint = "install a C++20 compiler, cmake, ninja-build and SDL3's X11/Wayland/audio development packages"
    else:
        compiler = "cl"
        hint = ("run from the x64 Native Tools Command Prompt for Visual Studio 2022, "
                "which provides cl, cmake and ninja")
    for tool in ("cmake", "ninja", compiler):
        if shutil.which(tool) is None:
            raise ValueError(f"Missing {tool}; {hint}")


def configure_command(game_module, *extra):
    command = ["cmake", "-S", ROOT, "-B", BUILD, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
               "-DPSPRECOMP_PROFILE=sarusaru", f"-DSARUSARU_GAME_MODULE={game_module}", *extra]
    if host_name() == "macos-arm64":
        command.append("-DCMAKE_OSX_ARCHITECTURES=arm64")
    return command


@contextlib.contextmanager
def exclusive_lock(path, busy_message):
    """Holds a non-blocking exclusive lock on <path>, or fails with <busy_message>."""
    with Path(path).open("a") as lock:
        try:
            if sys.platform == "win32":
                import msvcrt
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise ValueError(busy_message) from exc
        yield


def bootstrap(iso, jobs):
    check_toolchain()
    supported = json.loads((PROFILE / "config/supported_game.json").read_text())
    run(sys.executable, PROFILE / "tools/prepare_game.py", iso)
    elf = PROFILE / "game/BOOT.elf"
    if hashlib.sha256(elf.read_bytes()).hexdigest() != supported["executable_sha256"]:
        raise ValueError("Prepared executable identity changed")

    def configure(enabled):
        run(*configure_command(enabled, "-DPSPRECOMP_BUILD_TESTS=ON", "-DPSPRECOMP_BUILD_PROFILE_TESTS=ON"))

    configure("OFF")
    run("cmake", "--build", BUILD, "--parallel", jobs,
        "--target", "psp_analyze", "psp_recomp", "psprecomp_tests")
    run("ctest", "--test-dir", BUILD, "--output-on-failure")
    analysis = PROFILE / "analysis"
    analysis.mkdir(exist_ok=True)
    run(executable(BUILD / "bin/psp_analyze"), elf, analysis / "main.json", supported["load_base"])
    run(executable(BUILD / "bin/psp_recomp"), elf, "--auto", PROFILE / "generated",
        supported["load_base"], supported["unit_span"])
    configure("ON")
    run("cmake", "--build", BUILD, "--parallel", jobs, "--target", "sarusaru_bootstrap")
    run("ctest", "--test-dir", BUILD, "--output-on-failure")

    print("Running diagnostic host with a 20-second wall-clock timeout.", flush=True)
    command = [str(executable(BUILD / "bin/sarusaru_bootstrap")), str(elf), str(iso)]
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
        with exclusive_lock(progress / "bootstrap.lock", "Another SaruSaru bootstrap is already running"):
            return bootstrap(args.iso.resolve(), args.jobs)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"bootstrap: {exc}\n")


if __name__ == "__main__":
    sys.exit(main())
