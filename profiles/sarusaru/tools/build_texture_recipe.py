#!/usr/bin/env python3
"""Build an HD texture recipe from texture dumps and the maintainer's own disc.

A recipe says what to upscale and how; it never holds pixels (see
docs/hd-textures.md and host/textures/recipe.hpp). Each dumped texture
(sarusaru --dump-textures DIR) is looked for on the disc as a TIM2 picture,
directly or inside .lzs files and PAC archives, and listed with where it was
found, so players' launchers read it from their own disc. Textures not found
there are listed with "-": the game collects them while the player plays.

    build_texture_recipe.py --iso GAME.iso --dump DUMP [--dump DUMP...] \\
        [--scan PSP_GAME/USRDIR/data] [--overrides overrides.txt] -o recipe.txt

The overrides file has one line per texture: `<hash> key=value...` to change
its options, or `<hash> skip` to leave it out (text the game draws at
runtime, gradients, anything the model spoils). The dumps' PNGs are the
maintainer's own and stay private; only the recipe is published.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import zlib

from prepare_game import IsoImage
from translation_assets import archive_members, decode_lzs

MAGIC = "sarusaru-texture-recipe"
OPTIONS = {
    "model": {"realesrgan-x4plus", "realesrgan-x4plus-anime", "realesr-animevideov3", "nearest"},
    "scale": {"1", "2", "4", "8"},
    "alpha": {"model", "linear", "nearest", "opaque"},
    "edge": {"clamp", "wrap"},
}
DEFAULTS = {"model": "realesrgan-x4plus-anime", "scale": "8", "alpha": "model", "edge": "clamp"}
SKIPPED_SUFFIXES = (".pmf", ".at3", ".sgb", ".sgh", ".csv", ".png", ".bin", ".prx")


def identity(width, height, rgba):
    """gpu::identify_texture for a logical rectangle (host/gpu/texture_hash.cpp)."""
    return hashlib.sha256(b"SSHD" + struct.pack("<HH", width, height) + rgba).hexdigest()[:32]


def is_hash(text):
    return len(text) == 32 and all(c in "0123456789abcdef" for c in text)


def parse_options(parts):
    result = {}
    for part in parts:
        key, sep, value = part.partition("=")
        if not sep or key not in OPTIONS or value not in OPTIONS[key]:
            raise ValueError(f"invalid option {part!r}")
        if key in result:
            raise ValueError(f"option {key} given twice")
        result[key] = value
    return result


def read_png(path):
    """(width, height, RGBA bytes) of an 8-bit RGB or RGBA PNG, without Pillow."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    at, idat, header = 8, bytearray(), None
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
    if header is None:
        raise ValueError(f"{path} has no header")
    width, height, depth, colour, _, _, interlace = header
    if depth != 8 or colour not in (2, 6) or interlace:
        raise ValueError(f"{path}: only 8-bit RGB/RGBA PNGs are read")
    channels = 4 if colour == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray()
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        kind = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        previous = line
        if channels == 4:
            out += line
        else:
            for x in range(0, stride, 3):
                out += line[x:x + 3] + b"\xff"
    return width, height, bytes(out)


def tim2_size(data):
    """(width, height) of a TIM2 picture the launcher can decode, else None."""
    if len(data) < 64 or data[:4] != b"TIM2" or data[4] != 4 or data[5] != 0 or struct.unpack_from("<H", data, 6)[0] != 1:
        return None
    clut, kind, width, height = data[34], data[35], *struct.unpack_from("<HH", data, 36)
    if kind not in (4, 5) or clut & 0x3F != 3 or not 0 < width <= 512 or not 0 < height <= 512:
        return None
    return width, height


def decode_tim2(data):
    """RGBA of the top picture, as host/textures/disc_textures.cpp decodes it, else None."""
    if tim2_size(data) is None:
        return None
    total, palette_size, image_size, header_size, colors, _, mips, clut, kind, width, height = \
        struct.unpack_from("<IIIHHBBBBHH", data, 16)
    bits = 4 if kind == 4 else 8
    row = width * bits // 8
    if row % 16 or height % 8 or not mips or header_size < 48:
        return None
    if (colors < 1 << bits or palette_size < colors * 4 or image_size < row * height
            or header_size + image_size + palette_size > total or 16 + total > len(data)):
        return None
    raw = data[16 + header_size:16 + header_size + row * height]
    palette = data[16 + header_size + image_size:16 + header_size + image_size + palette_size]
    csm1 = not clut & 0x40
    out = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            byte_x = x * bits // 8
            index = ((y // 8) * (row // 16) + byte_x // 16) * 128 + (y % 8) * 16 + byte_x % 16
            color = (raw[index] >> ((x % 2) * 4)) & 15 if bits == 4 else raw[index]
            if bits == 8 and csm1:
                color = (color & ~24) | ((color & 8) << 1) | ((color & 16) >> 1)
            at = (y * width + x) * 4
            out[at:at + 4] = palette[color * 4:color * 4 + 4]
    return width, height, bytes(out)


def scan_pictures(blob, locator, wanted_sizes, found, depth=0):
    """Yields (locator, hash) for TIM2 pictures of a wanted size, as scan_disc() walks them."""
    try:
        members = archive_members(blob)
    except ValueError:
        members = None
    if members is not None:
        if depth >= 4:
            return
        for name in sorted(members):
            inner = f"{locator}|{name}"
            member = members[name]
            if name.lower().endswith(".lzs"):
                try:
                    scan_pictures(decode_lzs(member), inner + "!lzs", wanted_sizes, found, depth + 1)
                    continue
                except ValueError:
                    pass
            scan_pictures(member, inner, wanted_sizes, found, depth + 1)
        return
    at = blob.find(b"TIM2")
    while at != -1:
        size = tim2_size(blob[at:at + 64])
        if size in wanted_sizes and size[1] & (size[1] - 1) == 0:
            picture = decode_tim2(blob[at:])
            if picture:
                found.setdefault(identity(*picture), f"{locator}#{at:x}")
        at = blob.find(b"TIM2", at + 4)


def locate(iso_path, wanted_sizes, root="PSP_GAME/USRDIR"):
    """Hash -> source locator for every decodable picture of a wanted size under `root`."""
    found = {}
    with open(iso_path, "rb") as stream:
        iso = IsoImage(stream)
        for entry in iso.inventory():
            path = entry["path"]
            if entry["directory"] or not path.startswith(root + "/") or path.lower().endswith(SKIPPED_SUFFIXES):
                continue
            if not 64 <= entry["size"] <= 32 * 1024 * 1024:
                continue
            record = iso.lookup(path)
            blob = iso.read(record.offset, record.size)
            locator = path
            if path.lower().endswith((".lzs", ".pac")):
                try:
                    blob, locator = decode_lzs(blob), path + "!lzs"
                except ValueError:
                    pass
            scan_pictures(blob, locator, wanted_sizes, found)
    return found


def read_dump(directory):
    """Hash -> (width, height, RGBA) for a --dump-textures directory."""
    directory = Path(directory)
    result = {}
    index = (directory / "textures.tsv").read_text(encoding="utf-8").splitlines()
    for line in index[1:]:
        fields = line.split("\t")
        if len(fields) < 3 or not is_hash(fields[0]):
            continue
        png = directory / (fields[0] + ".png")
        if png.is_file():
            result[fields[0]] = read_png(png)
    return result


def read_overrides(path):
    """Hash -> options dict, or None for a skipped texture."""
    result = {}
    for number, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        parts = line.split()
        if not parts or parts[0].startswith("#"):
            continue
        if not is_hash(parts[0]) or len(parts) < 2:
            raise ValueError(f"{path}:{number}: expected a hash and options or skip")
        result[parts[0]] = None if parts[1:] == ["skip"] else parse_options(parts[1:])
    return result


def options_text(options):
    return "".join(f"\t{key}={options[key]}" for key in sorted(options))


def build(textures, locations, disc_id, defaults, scans=(), overrides=None, disc_only=False):
    """The recipe text for `textures` (hash -> (w, h, rgba)) and their `locations`."""
    overrides = overrides or {}
    lines = ["# SaruSaru texture recipe: what to upscale and how. No pixels.", f"{MAGIC}\t1", f"disc\t{disc_id}"]
    lines.append("defaults" + options_text(defaults))
    lines += [f"scan\t{scan}" for scan in scans]
    skipped = sorted(h for h, value in overrides.items() if value is None)
    for texture_hash in sorted(textures):
        if texture_hash in skipped:
            continue
        source = locations.get(texture_hash)
        if source is None and disc_only:
            continue
        options = {}
        rgba = textures[texture_hash][2]
        if all(rgba[i] == 255 for i in range(3, len(rgba), 4)) and defaults.get("alpha") != "opaque":
            options["alpha"] = "opaque"
        options.update(overrides.get(texture_hash) or {})
        options = {k: v for k, v in options.items() if defaults.get(k) != v}
        lines.append(f"texture\t{texture_hash}\t{source or '-'}" + options_text(options))
    lines += [f"skip\t{h}" for h in skipped]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", type=Path, required=True, help="the maintainer's own disc image")
    parser.add_argument("--dump", type=Path, action="append", required=True, help="a --dump-textures directory")
    parser.add_argument("--disc-id", default="UCJS10060")
    parser.add_argument("--scan", action="append", default=[], help="disc directory the launcher scans for every picture")
    parser.add_argument("--overrides", type=Path, help="per-texture options and skips")
    parser.add_argument("--defaults", default=" ".join(f"{k}={v}" for k, v in DEFAULTS.items()))
    parser.add_argument("--disc-only", action="store_true", help="leave out textures not found on the disc")
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        defaults = parse_options(args.defaults.split())
        textures = {}
        for dump in args.dump:
            textures.update(read_dump(dump))
        overrides = read_overrides(args.overrides) if args.overrides else {}
        sizes = {(w, h) for w, h, _ in textures.values()}
        locations = {h: s for h, s in locate(args.iso, sizes).items() if h in textures}
        recipe = build(textures, locations, args.disc_id, defaults, args.scan, overrides, args.disc_only)
        args.output.write_text(recipe, encoding="utf-8")
        listed = recipe.count("\ntexture\t")
        print(f"{args.output}: {listed} textures, {len(locations)} found on the disc, "
              f"{listed - len(locations)} collected while playing, "
              f"{sum(1 for v in overrides.values() if v is None)} skipped; "
              f"sha256 {hashlib.sha256(recipe.encode()).hexdigest()}, {len(recipe.encode())} bytes")
    except (OSError, ValueError, zlib.error) as exc:
        parser.exit(1, f"build_texture_recipe: {exc}\n")


if __name__ == "__main__":
    sys.exit(main())
