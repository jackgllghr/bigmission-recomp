"""The HD texture recipe tool, on invented pictures; no game data is involved."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_texture_recipe as tool


def tim2(width, height, palette, index):
    """A 4-bit swizzled TIM2 whose texel (x, y) is palette[index(x, y)]."""
    row = width // 2
    image = bytearray(row * height)
    for y in range(height):
        for x in range(width):
            at = ((y // 8) * (row // 16) + (x // 2) // 16) * 128 + (y % 8) * 16 + (x // 2) % 16
            image[at] |= index(x, y) << ((x % 2) * 4)
    pal = b"".join(struct.pack("<I", c) for c in palette)
    header = struct.pack("<IIIHHBBBBHH", 48 + len(image) + len(pal), len(pal), len(image), 48, len(palette), 0, 1, 3, 4,
                         width, height)
    return b"TIM2\x04\x00\x01\x00" + bytes(8) + header + bytes(48 - len(header)) + bytes(image) + pal


def pac(members):
    table = bytearray(48 * len(members))
    payload = bytearray()
    for i, (name, data) in enumerate(members):
        struct.pack_into("<4I", table, i * 48, 0, len(table) + len(payload), len(data), i)
        table[i * 48 + 16:i * 48 + 16 + len(name)] = name.encode()
        payload += data
    table[0:2] = struct.pack("<H", len(members))
    return bytes(table + payload)


def lzs(data):
    out = bytearray(struct.pack("<I", len(data)))
    for i in range(0, len(data), 8):
        out += b"\xff" + data[i:i + 8]
    return bytes(out)


def png(width, height, rgba):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


PALETTE = [0xFF000000 | i * 0x0F0F0F for i in range(16)]
OPAQUE = tim2(32, 8, PALETTE, lambda x, y: (x + y) % 16)
SEETHROUGH = tim2(32, 16, [c & 0x00FFFFFF if i == 0 else c for i, c in enumerate(PALETTE)], lambda x, y: x % 16)


class RecipeToolTests(unittest.TestCase):
    def test_identity_matches_the_renderer(self):
        # The same value tests/hd_texture_tests.cpp checks in C++.
        self.assertEqual(tool.identity(2, 1, bytes(range(1, 9))), "8abe573ea62b1337bca08735b63c811d")

    def test_decodes_and_locates_pictures_in_archives(self):
        width, height, rgba = tool.decode_tim2(OPAQUE)
        self.assertEqual((width, height), (32, 8))
        self.assertEqual(rgba[4 * 3:4 * 4], struct.pack("<I", PALETTE[3]))
        blob = tool.decode_lzs(lzs(pac([("a.tm2", OPAQUE), ("b.tm2", SEETHROUGH)])))
        found = {}
        tool.scan_pictures(blob, "PSP_GAME/USRDIR/x.lzs!lzs", {(32, 8), (32, 16)}, found)
        self.assertEqual(found[tool.identity(*tool.decode_tim2(OPAQUE))], "PSP_GAME/USRDIR/x.lzs!lzs|a.tm2#0")
        self.assertEqual(len(found), 2)
        found = {}
        tool.scan_pictures(bytes(100) + OPAQUE, "PSP_GAME/USRDIR/loose.dat", {(32, 8)}, found)
        self.assertEqual(list(found.values()), ["PSP_GAME/USRDIR/loose.dat#64"])

    def test_reads_dumped_pngs(self):
        rgba = bytes(range(256))[:4 * 8]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "t.png"
            path.write_bytes(png(4, 2, rgba))
            self.assertEqual(tool.read_png(path), (4, 2, rgba))

    def test_recipe_lines(self):
        opaque = tool.decode_tim2(OPAQUE)
        clear = tool.decode_tim2(SEETHROUGH)
        textures = {tool.identity(*opaque): opaque, tool.identity(*clear): clear, "f" * 32: (1, 1, b"\0\0\0\0")}
        locations = {tool.identity(*opaque): "PSP_GAME/USRDIR/x.lzs!lzs|a.tm2#0"}
        overrides = {tool.identity(*clear): {"edge": "wrap"}, "f" * 32: None}
        text = tool.build(textures, locations, "TEST00001", dict(tool.DEFAULTS), ["PSP_GAME/USRDIR/data"], overrides)
        lines = text.splitlines()
        self.assertIn("sarusaru-texture-recipe\t1", lines)
        self.assertIn("scan\tPSP_GAME/USRDIR/data", lines)
        self.assertIn(f"texture\t{tool.identity(*opaque)}\tPSP_GAME/USRDIR/x.lzs!lzs|a.tm2#0\talpha=opaque", lines)
        self.assertIn(f"texture\t{tool.identity(*clear)}\t-\tedge=wrap", lines)
        self.assertIn(f"skip\t{'f' * 32}", lines)
        self.assertNotIn("PNG", text)
        disc_only = tool.build(textures, locations, "TEST00001", dict(tool.DEFAULTS), disc_only=True)
        self.assertEqual(disc_only.count("\ntexture\t"), 1)

    def test_rejects_unknown_options(self):
        with self.assertRaises(ValueError):
            tool.parse_options(["model=waifu2x"])
        with self.assertRaises(ValueError):
            tool.parse_options(["scale=3"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "overrides.txt"
            path.write_text("not-a-hash skip\n")
            with self.assertRaises(ValueError):
                tool.read_overrides(path)


if __name__ == "__main__":
    unittest.main()
