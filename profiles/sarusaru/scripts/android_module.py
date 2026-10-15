#!/usr/bin/env python3
"""Build the Android game module on this computer from your own disc and push it to a phone.

For development, until the app can build the game on the phone itself: the
same steps as the launcher's installer (host/install/game_builder.cpp), with
the NDK's clang, against the runtime of the APK's own native build so the
module's interface hash matches. The module and the code generated for it stay
in out/android-module/ (ignored) and on the phone; neither is ever packaged.
"""

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
sys.path.insert(0, str(PROFILE / "tools"))
from prepare_game import IsoImage, validate_game  # noqa: E402

PACKAGE = "io.github.sarusaru_recomp.SaruSaruRecomp"
API_LEVEL = 29
MODULE = "sarusaru_game.so"


def run(*command, **options):
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], check=True, **options)


def header_string(path, name):
    match = re.search(rf'{name} = "([^"]*)"', path.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(f"android_module: no {name} in {path}")
    return match.group(1)


def android_build_directory(given):
    if given:
        return given.resolve()
    # The APK's native build: Gradle's CMake directory for its debug variant.
    candidates = sorted((PROFILE / "packaging/android/app/.cxx").glob("*/*/arm64-v8a/bin/sdk"),
                        key=lambda path: path.stat().st_mtime, reverse=True)
    if not candidates:
        raise SystemExit("android_module: build the APK first (packaging/android: ./gradlew assembleDebug)")
    return candidates[0].parents[1]


def ndk_clang(sdk):
    ndk_root = sdk / "ndk"
    versions = sorted(ndk_root.iterdir()) if ndk_root.is_dir() else []
    for ndk in reversed(versions):
        for host in ("darwin-x86_64", "linux-x86_64"):
            clang = ndk / "toolchains/llvm/prebuilt" / host / "bin/clang++"
            if clang.is_file():
                return clang
    raise SystemExit(f"android_module: no NDK clang++ under {ndk_root}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, help="Game ISO (default: saved game/input.json path)")
    parser.add_argument("--android-build", type=Path, help="The APK's CMake build directory (default: Gradle's)")
    parser.add_argument("--host-build", type=Path, default=ROOT / "out/macos-arm64",
                        help="A build of this computer with psp_recomp (default: out/macos-arm64)")
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("ANDROID_HOME",
                        Path.home() / "Library/Android/sdk")), help="The Android SDK")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--no-push", action="store_true", help="Only build the module")
    args = parser.parse_args()

    iso = args.iso
    if iso is None:
        metadata = PROFILE / "game/input.json"
        if not metadata.is_file():
            raise SystemExit("android_module: no saved ISO path; supply --iso /path/to/game.iso")
        iso = Path(json.loads(metadata.read_text(encoding="utf-8"))["iso_path"])
    android = android_build_directory(args.android_build)
    sdk_include = android / "bin/sdk/include"
    runtime = android / "bin/lib/libpsprecomp_runtime.so"
    config = next(android.glob("profiles/sarusaru/install/game_build_config.hpp"))
    abi = header_string(sdk_include / "game_abi.hpp", "game_module_abi")
    clang = ndk_clang(args.sdk)
    work = ROOT / "out/android-module"

    # 1. The executable, from the verified disc.
    supported = json.loads((PROFILE / "config/supported_game.json").read_text(encoding="utf-8"))
    with iso.open("rb") as stream:
        executable, _ = validate_game(IsoImage(stream), supported)
    shutil.rmtree(work, ignore_errors=True)
    (work / "obj").mkdir(parents=True)
    elf = work / "BOOT.elf"
    elf.write_bytes(executable)

    # 2. Its code, from this computer's code generator (the same source as the APK's).
    run("cmake", "--build", args.host_build, "--target", "psp_recomp")
    generated = work / "generated"
    run(args.host_build / "bin/psp_recomp", elf, "--auto", generated, supported["load_base"], supported["unit_span"])
    elf.unlink()

    # 3. Every unit, then the module's interface, with the flags the APK's build records.
    flags = [f"--target=aarch64-linux-android{API_LEVEL}", "-std=c++20", "-fvisibility=hidden", "-fPIC",
             *header_string(config, "kOptimization").split(),
             *(f"-D{define}" for define in header_string(config, "kDefines").split()), f"-I{sdk_include}"]
    sources = sorted(generated.glob("generated_*.cpp")) + [android / "bin/sdk/game_module_entry.cpp"]

    def compile_unit(source):
        target = work / "obj" / (source.stem + ".o")
        subprocess.run([str(clang), "-c", *flags, str(source), "-o", str(target)], check=True)
        return target

    print(f"Compiling {len(sources)} units with {args.jobs} jobs", flush=True)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        objects = list(pool.map(compile_unit, sources))
    module = work / MODULE
    # The NDK's clang++ links libc++_shared, the C++ runtime the APK ships.
    run(clang, f"--target=aarch64-linux-android{API_LEVEL}", "-shared", "-Wl,-z,max-page-size=16384",
        "-o", module, *objects, runtime)
    print(f"Built {module} for interface {abi}")
    if args.no_push:
        return

    # 4. Into the app's own storage, where the launcher looks for an installed game.
    adb = args.sdk / "platform-tools/adb"
    staging = f"/data/local/tmp/{MODULE}"
    target = f"files/game/{abi}"
    run(adb, "push", module, staging)
    run(adb, "shell", f"run-as {PACKAGE} sh -c 'mkdir -p {target} && cp {staging} {target}/{MODULE} "
        f"&& chmod 700 files/game {target} && chmod 600 {target}/{MODULE}'")
    run(adb, "shell", "rm", staging)
    print(f"Pushed to {PACKAGE}: {target}/{MODULE}")


if __name__ == "__main__":
    main()
