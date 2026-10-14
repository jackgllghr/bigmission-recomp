#!/usr/bin/env python3
"""Package a built launcher for players: a .zip (macOS app, Windows) or .tar.gz (Linux).

The package holds the launcher, its libraries and resources, and what it needs
to build the game on the player's machine from their own disc: the code
generator and the runtime's headers (sdk/). It never holds anything derived
from the game: no generated code and no game module, which the script refuses
to package. Nor does it hold a translation: the English game text is a separate,
optional download the launcher offers (sarusaru-translations). Nor textures:
dumps and HD packs are the game's art, made on the player's device.
"""

import argparse
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bootstrap import BUILD, host_name  # noqa: E402

NAME = "SaruSaruRecomp"
APP_NAME = "SaruSaru Recomp"
BUNDLE_ID = "io.github.sarusaru-recomp.launcher"

# Built files that are for developers only.
DEVELOPER_ONLY = ("sarusaru_bootstrap", "sarusaru_vulkan_probe", "psp_analyze", "glslang", "glslangValidator")

PLAYER_README = """\
SaruSaru Recomp {version} ({host})

An unofficial, fan-made native port of Saru! Get You! SaruSaru Daisakusen
(UCJS10060). It contains no game files: you need your own disc image (.iso)
of the Japanese UMD.

Start the launcher, choose your disc image, and choose Play. The first time,
the launcher builds the game for your computer from your disc, which takes a
few minutes. It needs:
  macOS    Apple's Command Line Tools (the launcher asks macOS to install them)
  Linux    a C++ compiler: g++ or clang++ from your distribution
  Windows  nothing; it downloads the llvm-mingw compiler (about 190 MB) once

{run}

The game's text is Japanese. Settings > Interface > Game text can switch it to
an unofficial English machine translation, downloaded when first chosen.

Not affiliated with or endorsed by Sony Interactive Entertainment. See
LICENSE.txt and THIRD_PARTY.md for the licences of this port and of the
libraries and fonts it includes.
"""

RUN = {
    "macos": ("Open SaruSaru Recomp.app. It is not notarised by Apple, so the first time\n"
              "right-click it and choose Open, or run:\n"
              "  xattr -dr com.apple.quarantine \"SaruSaru Recomp.app\""),
    "linux": "Run ./sarusaru from this folder. Vulkan drivers are needed (Mesa or your GPU vendor's).",
    "windows": "Run sarusaru.exe from this folder.",
}


def is_game_derived(path):
    name = path.name
    return name.startswith("sarusaru_game") or name.startswith("generated_") or name == "BOOT.elf"


def is_translation(path):
    """A translation pack or catalog: published apart from the port, never in it."""
    return path.suffix == ".sstp" or (path.suffix == ".json" and path.name.startswith("english-"))


def is_texture_set(path):
    """A texture dump or HD texture pack (docs/hd-textures.md): game art, never packaged."""
    stem = path.name.split(".")[0]
    upscaled = path.suffix == ".png" and len(stem) == 32 and all(c in "0123456789abcdef" for c in stem)
    return path.name in ("textures.tsv", "pack.ini", "wanted.txt") or upscaled


def player_files(bin_dir, windows):
    """(source, path in package) for everything a player gets from bin/."""
    exe = ".exe" if windows else ""
    picked = []
    for name in ("sarusaru", "psp_recomp"):
        source = bin_dir / (name + exe)
        if not source.is_file():
            raise ValueError(f"{source} is missing; build the sarusaru and psp_recomp targets first")
        picked.append((source, Path(source.name)))
    for folder in ("lib", "resources", "sdk"):
        for source in sorted((bin_dir / folder).rglob("*")):
            if source.is_file():
                picked.append((source, source.relative_to(bin_dir)))
    if windows:
        for source in sorted(bin_dir.glob("*.dll")):
            picked.append((source, Path(source.name)))
        picked.extend((source, Path(source.name)) for source in sorted(bin_dir.glob("*LICENSE*.txt")))
        picked.extend((source, Path(source.name)) for source in sorted(bin_dir.glob("FFmpeg-SOURCE.txt")))
    if not (bin_dir / "sdk/include/game_abi.hpp").is_file():
        raise ValueError("bin/sdk is incomplete; reconfigure the build")
    # Nothing may name the machine it was built on (rpaths, embedded paths).
    leak = str(bin_dir.parent).encode()
    for source, _ in picked:
        if source.suffix in ("", ".exe", ".dll", ".dylib", ".so") or ".so." in source.name:
            if leak in source.read_bytes():
                raise ValueError(f"{source} contains the build directory {bin_dir.parent}")
        if is_game_derived(source) or any(part == "generated" for part in source.parts[len(bin_dir.parts):]):
            raise ValueError(f"refusing to package game-derived file {source}")
        if is_translation(source) or any(part == "translations" for part in source.parts[len(bin_dir.parts):]):
            raise ValueError(f"refusing to package translation file {source}; it is a separate download")
        if is_texture_set(source) or any(part == "textures" for part in source.parts[len(bin_dir.parts):]):
            raise ValueError(f"refusing to package texture file {source}; textures are made on the player's device")
        if source.stem in DEVELOPER_ONLY:
            raise ValueError(f"unexpected developer file {source}")
    return picked


def stage(build, version, host, out_dir):
    windows = host.startswith("windows")
    macos = host.startswith("macos")
    files = player_files(build / "bin", windows)
    work = Path(tempfile.mkdtemp(prefix="sarusaru-package-"))
    top = work / f"{NAME}-{version}-{host}"
    app = top / f"{APP_NAME}.app"
    for source, relative in files:
        target = app / bundle_path(relative) if macos else top / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if macos and source.is_symlink():
            target.symlink_to(os.readlink(source))
        else:
            shutil.copy2(source, target, follow_symlinks=True)
    if macos:
        bundle(app, version)
    shutil.copy2(ROOT / "LICENSE", top / "LICENSE.txt")
    shutil.copy2(PROFILE / "THIRD_PARTY.md", top / "THIRD_PARTY.md")
    family = "macos" if macos else "windows" if windows else "linux"
    readme = PLAYER_README.format(version=version, host=host, run=RUN[family])
    (top / "README.txt").write_text(readme.replace("\n", "\r\n") if windows else readme, encoding="utf-8")
    out_dir.mkdir(parents=True, exist_ok=True)
    return work, top


def bundle_path(relative):
    """Where a file of bin/ goes in the macOS app, as platform/paths.cpp expects: programs in
    Contents/MacOS, libraries in Contents/Frameworks, everything else in Contents/Resources
    (only code may be in MacOS and Frameworks for the bundle to be signed)."""
    if len(relative.parts) == 1:
        return Path("Contents/MacOS") / relative
    if relative.parts[0] == "lib" and relative.suffix == ".dylib":
        return Path("Contents/Frameworks") / relative.name
    return Path("Contents/Resources") / relative


def bundle(app, version):
    """Info.plist and icon for the macOS app, then an ad-hoc signature over the whole bundle.

    The signature is not Apple's (the app is not notarised), but without one
    macOS calls a downloaded app damaged rather than offering to open it."""
    contents = app / "Contents"
    (contents / "Resources").mkdir(parents=True, exist_ok=True)
    icon = make_icns(contents / "Resources")
    info = {
        "CFBundleName": APP_NAME,
        "CFBundleDisplayName": APP_NAME,
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleExecutable": "sarusaru",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version.lstrip("v"),
        "CFBundleVersion": version.lstrip("v"),
        "LSMinimumSystemVersion": os.environ.get("MACOSX_DEPLOYMENT_TARGET", "13.0"),
        "NSHighResolutionCapable": True,
        "LSApplicationCategoryType": "public.app-category.games",
    }
    if icon:
        info["CFBundleIconFile"] = icon
    with (contents / "Info.plist").open("wb") as out:
        plistlib.dump(info, out)
    if shutil.which("codesign") is not None:
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", "--timestamp=none", str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)


def ico_images(path):
    """{pixels: PNG bytes} of the square PNG images in a Windows .ico."""
    data = path.read_bytes()
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if reserved != 0 or kind != 1:
        raise ValueError(f"{path} is not an icon file")
    images = {}
    for index in range(count):
        width, height, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * index)
        png = data[offset:offset + size]
        if width == height and png.startswith(b"\x89PNG\r\n\x1a\n"):
            images[width or 256] = png
    return images


def make_icns(resources):
    """The Windows icon (resources/sarusaru.ico) as an .icns, where macOS's iconutil is available.

    Each slot of the iconset takes the .ico's image of exactly that size; slots
    it has no image for are left out rather than scaled up."""
    if shutil.which("iconutil") is None:
        return None
    images = ico_images(PROFILE / "resources/sarusaru.ico")
    with tempfile.TemporaryDirectory() as temporary:
        iconset = Path(temporary) / "sarusaru.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                png = images.get(size * scale)
                if png is not None:
                    (iconset / f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png").write_bytes(png)
        if not any(iconset.iterdir()):
            raise ValueError("resources/sarusaru.ico has no PNG images of an .icns size")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(resources / "sarusaru.icns")], check=True)
    return "sarusaru.icns"


def archive(work, top, out_dir, host):
    if host.startswith("linux"):
        path = out_dir / f"{top.name}.tar.gz"
        with tarfile.open(path, "w:gz") as tar:
            tar.add(top, arcname=top.name)
    elif host.startswith("macos"):
        # ditto keeps the bundle's permissions and attributes as Finder expects.
        path = out_dir / f"{top.name}.zip"
        subprocess.run(["ditto", "-c", "-k", "--norsrc", "--noextattr", "--noqtn", "--keepParent", str(top), str(path)], check=True)
    else:
        path = out_dir / f"{top.name}.zip"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
            for file in sorted(top.rglob("*")):
                if file.is_file():
                    zipped.write(file, Path(top.name) / file.relative_to(top))
    shutil.rmtree(work)
    return path


def default_version():
    try:
        return subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, check=True,
                              capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "dev"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, default=BUILD, help="Build directory (default: out/<host>)")
    parser.add_argument("--host", help="Host the build is for (default: this machine's)")
    parser.add_argument("--version", default=None, help="Version in the name (default: git describe)")
    parser.add_argument("--out", type=Path, default=ROOT / "out/dist", help="Where the archive goes")
    args = parser.parse_args()
    try:
        host = args.host or host_name()
        version = args.version or default_version()
        work, top = stage(args.build.resolve(), version, host, args.out)
        path = archive(work, top, args.out, host)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"package: {exc}\n")
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
