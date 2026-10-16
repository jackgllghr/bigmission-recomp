#!/usr/bin/env python3
"""Package a finished HD texture pack directory as the zip a build offers.

The pack is a directory the renderer loads (host/textures/hd_pack.hpp):
pack.ini and one <hash>.png per texture, made with the launcher's HD textures
screen or by hand. The zip is published apart from the port, like the English
pack; the launcher downloads it, checks its SHA-256, unpacks it and checks that
pack.ini names the player's disc (docs/hd-textures.md, "Prebuilt pack").

    package_hd_pack.py PACK_DIR -o sarusaru-hd.zip

Only pack.ini and hash-named PNGs go in; wanted.txt, collected/ and anything
else stay out. Prints the SHA-256 and size to configure a build with.
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys
import zipfile

HASH = re.compile(r"[0-9a-f]{32}")


def read_manifest(path: Path) -> dict:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"{path}: expected key = value: {line}")
        values[key.strip()] = value.strip()
    if values.get("format") != "1" or not values.get("disc") or values.get("scale") not in {str(s) for s in range(2, 9)}:
        raise ValueError(f"{path}: needs format = 1, a disc ID and a scale of 2-8")
    return values


def package(pack: Path, output: Path) -> tuple[str, int, int]:
    manifest = read_manifest(pack / "pack.ini")
    images = sorted(p for p in pack.iterdir() if p.is_file() and p.suffix == ".png" and HASH.fullmatch(p.stem))
    if not images:
        raise ValueError(f"{pack}: no <hash>.png images")
    folder = f"sarusaru-hd-{manifest['scale']}x"
    partial = output.with_name(output.name + ".partial")
    # PNGs are compressed already; storing them keeps unpacking fast.
    with zipfile.ZipFile(partial, "w", zipfile.ZIP_STORED) as archive:
        archive.write(pack / "pack.ini", f"{folder}/pack.ini", compress_type=zipfile.ZIP_DEFLATED)
        for image in images:
            archive.write(image, f"{folder}/{image.name}")
    partial.replace(output)
    data = output.read_bytes()
    return hashlib.sha256(data).hexdigest(), len(data), len(images)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pack", type=Path, help="the HD pack directory (pack.ini and <hash>.png)")
    parser.add_argument("-o", "--output", type=Path, required=True, help="the zip to write")
    args = parser.parse_args()
    try:
        sha256, size, count = package(args.pack, args.output)
    except (OSError, ValueError) as error:
        print(f"package_hd_pack: {error}", file=sys.stderr)
        return 1
    print(f"{args.output}: {count} textures, {size} bytes")
    print(f"-DSARUSARU_HD_PACK_URL=https://.../{args.output.name} "
          f"-DSARUSARU_HD_PACK_SHA256={sha256} -DSARUSARU_HD_PACK_SIZE={size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
