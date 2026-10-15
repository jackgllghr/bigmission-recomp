#!/usr/bin/env python3
"""Package a built launcher for players: a .zip (macOS app, Windows), or on Linux a
.tar.gz, an AppImage (--format appimage) or an installed prefix (--format prefix,
which the Flatpak in packaging/linux uses).

The package holds the launcher, its libraries and resources, and what it needs
to build the game on the player's machine from their own disc: the code
generator and the runtime's headers (sdk/). It never holds anything derived
from the game: no generated code and no game module, which the script refuses
to package. Nor does it hold a translation: the English game text is a separate,
optional download the launcher offers (sarusaru-translations). Nor textures:
dumps and HD packs are the game's art, made on the player's device.
"""

import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import zipfile

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bootstrap import BUILD, host_name  # noqa: E402

NAME = "SaruSaruRecomp"
APP_NAME = "SaruSaru Recomp"
BUNDLE_ID = "io.github.sarusaru-recomp.launcher"
# The Linux desktop ID, which is also the Flatpak's.
APP_ID = "io.github.sarusaru_recomp.SaruSaruRecomp"
LINUX = PROFILE / "packaging/linux"

# appimagetool and the AppImage runtime it puts in front of the image, pinned.
# The runtime is static (needs no libfuse2 on the player's system).
APPIMAGETOOL = "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-{arch}.AppImage"
APPIMAGE_RUNTIME = "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-{arch}"
APPIMAGE_SHA256 = {
    ("appimagetool", "x86_64"): "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0",
    ("appimagetool", "aarch64"): "f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158",
    ("runtime", "x86_64"): "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d",
    ("runtime", "aarch64"): "00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444",
}
APPIMAGE_ARCH = {"linux-x64": "x86_64", "linux-arm64": "aarch64"}

# Built files that are for developers only.
DEVELOPER_ONLY = ("sarusaru_bootstrap", "sarusaru_vulkan_probe", "sarusaru_dump_movie_audio", "psp_analyze", "glslang", "glslangValidator")

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
    """A translation pack, catalog or movie subtitles: published apart from the port, never in it."""
    return path.suffix in (".sstp", ".srt") or (path.suffix == ".json" and path.name.startswith("english-"))


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


def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        target.symlink_to(os.readlink(source))
    else:
        shutil.copy2(source, target, follow_symlinks=True)


def install_linux(build, prefix):
    """The launcher installed under a Linux prefix (/app in the Flatpak, usr/ in the AppImage).

    Its files stay together in lib/sarusaru/, where platform/paths.cpp looks for
    them beside the executable, with bin/sarusaru linking to it; the desktop
    entry, icons, AppStream metadata and licences go under share/."""
    home = prefix / "lib/sarusaru"
    for source, relative in player_files(build / "bin", windows=False):
        copy_file(source, home / relative)
    (prefix / "bin").mkdir(parents=True, exist_ok=True)
    launcher = prefix / "bin/sarusaru"
    if launcher.is_symlink() or launcher.exists():
        launcher.unlink()
    launcher.symlink_to("../lib/sarusaru/sarusaru")
    share = prefix / "share"
    copy_file(LINUX / f"{APP_ID}.desktop", share / "applications" / f"{APP_ID}.desktop")
    copy_file(LINUX / f"{APP_ID}.metainfo.xml", share / "metainfo" / f"{APP_ID}.metainfo.xml")
    for pixels, png in sorted(ico_images(PROFILE / "resources/sarusaru.ico").items()):
        icon = share / f"icons/hicolor/{pixels}x{pixels}/apps/{APP_ID}.png"
        icon.parent.mkdir(parents=True, exist_ok=True)
        icon.write_bytes(png)
    copy_file(ROOT / "LICENSE", share / "licenses" / APP_ID / "LICENSE")
    copy_file(PROFILE / "THIRD_PARTY.md", share / "licenses" / APP_ID / "THIRD_PARTY.md")


def fetch_verified(url, sha256, path):
    """`url` at `path`, downloaded unless a file with the SHA-256 is already there."""
    if not (path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == sha256):
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != sha256:
            raise ValueError(f"{url} does not have the pinned SHA-256")
        path.write_bytes(data)
    path.chmod(0o755)
    return path


def make_appimage(build, version, host, out_dir):
    """An AppImage of the launcher: install_linux's layout under usr/, run through AppRun."""
    arch = APPIMAGE_ARCH.get(host)
    if arch is None:
        raise ValueError(f"no AppImage for {host}")
    tools = build / "appimage"
    tool = fetch_verified(APPIMAGETOOL.format(arch=arch), APPIMAGE_SHA256[("appimagetool", arch)],
                          tools / f"appimagetool-{arch}.AppImage")
    runtime = fetch_verified(APPIMAGE_RUNTIME.format(arch=arch), APPIMAGE_SHA256[("runtime", arch)],
                             tools / f"runtime-{arch}")
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{NAME}-{version}-{host}.AppImage"
    with tempfile.TemporaryDirectory(prefix="sarusaru-appimage-") as work:
        appdir = Path(work) / "AppDir"
        install_linux(build, appdir / "usr")
        (appdir / "AppRun").symlink_to("usr/bin/sarusaru")
        shutil.copy2(LINUX / f"{APP_ID}.desktop", appdir / f"{APP_ID}.desktop")
        (appdir / f"{APP_ID}.png").symlink_to(f"usr/share/icons/hicolor/256x256/apps/{APP_ID}.png")
        (appdir / ".DirIcon").symlink_to(f"{APP_ID}.png")
        # appimagetool is an AppImage itself; extracting it needs no FUSE (containers, CI).
        env = dict(os.environ, ARCH=arch, APPIMAGE_EXTRACT_AND_RUN="1")
        subprocess.run([str(tool), "--no-appstream", "--runtime-file", str(runtime), str(appdir), str(path)],
                       check=True, env=env, cwd=work)
    return path


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
    parser.add_argument("--format", choices=("archive", "appimage", "prefix"), default="archive",
                        help="archive (default); on Linux also appimage, or prefix: install into --prefix")
    parser.add_argument("--prefix", type=Path, help="Install prefix for --format prefix (/app in a Flatpak)")
    args = parser.parse_args()
    try:
        host = args.host or host_name()
        version = args.version or default_version()
        if args.format != "archive" and not host.startswith("linux"):
            raise ValueError(f"--format {args.format} is for Linux hosts only")
        if args.format == "prefix":
            if args.prefix is None:
                raise ValueError("--format prefix needs --prefix")
            install_linux(args.build.resolve(), args.prefix)
            path = args.prefix
        elif args.format == "appimage":
            path = make_appimage(args.build.resolve(), version, host, args.out.resolve())
        else:
            work, top = stage(args.build.resolve(), version, host, args.out)
            path = archive(work, top, args.out, host)
    except (OSError, ValueError, subprocess.CalledProcessError, urllib.error.URLError) as exc:
        parser.exit(1, f"package: {exc}\n")
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
