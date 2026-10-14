"""Public fixtures are invented ISO/SFO records, never extracted game bytes."""

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_game import (BLOCK, InvalidImage, IsoImage, install_inputs,
                          parse_sfo, validate_game)


def dual(value, width):
    return value.to_bytes(width, "little") + value.to_bytes(width, "big")


def record(name, lba, size, directory=False):
    name = name if isinstance(name, bytes) else name.encode("ascii")
    data = bytearray(33 + len(name) + (len(name) % 2 == 0))
    data[0] = len(data)
    data[2:10] = dual(lba, 4)
    data[10:18] = dual(size, 4)
    data[25] = 2 if directory else 0
    data[28:32] = dual(1, 2)
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return bytes(data)


def sfo(values):
    keys = bytearray()
    strings = bytearray()
    records = bytearray()
    for key, value in values.items():
        raw = value.encode("utf-8") + b"\0"
        records += struct.pack("<HHIII", len(keys), 0x204, len(raw), len(raw), len(strings))
        keys += key.encode("ascii") + b"\0"
        strings += raw
    key_start = 20 + len(records)
    return struct.pack("<4sIIII", b"\0PSF", 0x101, key_start, key_start + len(keys), len(values)) + records + keys + strings


def image_bytes():
    executable = b"\x7fELF\x01\x01\x01" + b"invented fixture"
    metadata = sfo({"DISC_ID": "TEST00001", "DISC_VERSION": "1.00"})
    data = bytearray(25 * BLOCK)
    pvd = bytearray(BLOCK)
    pvd[:7] = b"\x01CD001\x01"
    pvd[80:88] = dual(25, 4)
    pvd[128:132] = dual(BLOCK, 2)
    root = record(b"\0", 20, BLOCK, True)
    pvd[156:156 + len(root)] = root
    data[16 * BLOCK:17 * BLOCK] = pvd
    for lba, entries in (
        (20, [record("PSP_GAME", 21, BLOCK, True)]),
        (21, [record("PARAM.SFO;1", 23, len(metadata)), record("SYSDIR", 22, BLOCK, True)]),
        (22, [record("BOOT.BIN;1", 24, len(executable))]),
    ):
        raw = b"".join(entries)
        data[lba * BLOCK:lba * BLOCK + len(raw)] = raw
    data[23 * BLOCK:23 * BLOCK + len(metadata)] = metadata
    data[24 * BLOCK:24 * BLOCK + len(executable)] = executable
    supported = {
        "disc_id": "TEST00001", "disc_version": "1.00",
        "executable_path": "PSP_GAME/SYSDIR/BOOT.BIN",
        "executable_size": len(executable),
        "executable_sha256": hashlib.sha256(executable).hexdigest(),
    }
    return data, supported, executable


def symlink(link, target, directory=False):
    """Creates <link>, or skips the test where the user may not (Windows without Developer Mode)."""
    try:
        link.symlink_to(target, target_is_directory=directory)
    except OSError as exc:
        raise unittest.SkipTest(f"symlinks unavailable: {exc}")


class IsoTests(unittest.TestCase):
    def open_image(self, data):
        stream = tempfile.TemporaryFile()
        self.addCleanup(stream.close)
        stream.write(data)
        stream.flush()
        return IsoImage(stream)

    def test_valid_image(self):
        data, supported, executable = image_bytes()
        image = self.open_image(data)
        extracted, metadata = validate_game(image, supported)
        self.assertEqual(extracted, executable)
        self.assertEqual(metadata["DISC_ID"], "TEST00001")
        self.assertEqual(len(image.inventory()), 4)

    def test_wrong_disc(self):
        data, supported, _ = image_bytes()
        supported["disc_id"] = "OTHER0001"
        with self.assertRaisesRegex(InvalidImage, "Unsupported game"):
            validate_game(self.open_image(data), supported)

    def test_modified_executable(self):
        data, supported, _ = image_bytes()
        data[24 * BLOCK + 8] ^= 1
        with self.assertRaisesRegex(InvalidImage, "hash"):
            validate_game(self.open_image(data), supported)

    def test_truncated_volume(self):
        data, _, _ = image_bytes()
        with self.assertRaisesRegex(InvalidImage, "Truncated"):
            self.open_image(data[:-1])

    def test_inconsistent_endian_values(self):
        data, _, _ = image_bytes()
        data[16 * BLOCK + 87] ^= 1
        with self.assertRaisesRegex(InvalidImage, "encoding"):
            self.open_image(data)

    def test_out_of_bounds_extent(self):
        data, _, _ = image_bytes()
        data[20 * BLOCK + 2:20 * BLOCK + 10] = dual(100, 4)
        with self.assertRaisesRegex(InvalidImage, "outside"):
            self.open_image(data).inventory()

    def test_duplicate_filename(self):
        data, _, _ = image_bytes()
        entry = record("PSP_GAME", 21, BLOCK, True)
        data[20 * BLOCK:20 * BLOCK + 2 * len(entry)] = entry * 2
        with self.assertRaisesRegex(InvalidImage, "Duplicate"):
            self.open_image(data).inventory()

    def test_path_traversal(self):
        data, _, _ = image_bytes()
        entry = record("../escape", 21, BLOCK, True)
        data[20 * BLOCK:21 * BLOCK] = entry.ljust(BLOCK, b"\0")
        with self.assertRaisesRegex(InvalidImage, "Unsafe"):
            self.open_image(data).inventory()

    def test_directory_cycle(self):
        data, _, _ = image_bytes()
        entry = record("LOOP", 20, BLOCK, True)
        data[20 * BLOCK:21 * BLOCK] = entry.ljust(BLOCK, b"\0")
        with self.assertRaisesRegex(InvalidImage, "Cyclic"):
            self.open_image(data).inventory()

    def test_multi_extent_rejected(self):
        data, _, _ = image_bytes()
        data[22 * BLOCK + 25] |= 0x80
        with self.assertRaisesRegex(InvalidImage, "multi-extent"):
            self.open_image(data).inventory()

    def test_file_size_limit(self):
        data, _, _ = image_bytes()
        with self.assertRaisesRegex(InvalidImage, "oversized"):
            self.open_image(data).file("PSP_GAME/SYSDIR/BOOT.BIN", 1)

    def test_sfo_bounds(self):
        data = bytearray(sfo({"DISC_ID": "TEST00001"}))
        struct.pack_into("<I", data, 32, 0xFFFFFFFF)
        with self.assertRaisesRegex(InvalidImage, "entry"):
            parse_sfo(data)

    def test_sfo_invalid_utf8(self):
        data = bytearray(sfo({"DISC_ID": "TEST00001"}))
        data[-2] = 0xFF
        with self.assertRaisesRegex(InvalidImage, "encoding"):
            parse_sfo(data)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_install_is_idempotent(self):
        dest = self.root / "game"
        self.assertTrue(install_inputs(dest, b"invented", {}, [], self.root / "own.iso"))
        before = (dest / "BOOT.elf").stat().st_mtime_ns
        self.assertFalse(install_inputs(dest, b"invented", {}, [], self.root / "own.iso"))
        self.assertEqual(before, (dest / "BOOT.elf").stat().st_mtime_ns)

    def test_existing_different_input_preserved(self):
        dest = self.root / "game"
        install_inputs(dest, b"original", {}, [], self.root / "own.iso")
        with self.assertRaisesRegex(InvalidImage, "not overwritten"):
            install_inputs(dest, b"modified", {}, [], self.root / "own.iso")
        self.assertEqual((dest / "BOOT.elf").read_bytes(), b"original")

    def test_symlink_directory_rejected(self):
        target = self.root / "target"
        target.mkdir()
        dest = self.root / "game"
        symlink(dest, target, directory=True)
        with self.assertRaisesRegex(InvalidImage, "symlink"):
            install_inputs(dest, b"invented", {}, [], self.root / "own.iso")
        self.assertEqual(list(target.iterdir()), [])

    def test_symlink_executable_rejected(self):
        dest = self.root / "game"
        dest.mkdir()
        target = self.root / "valuable"
        target.write_bytes(b"invented")
        symlink(dest / "BOOT.elf", target)
        with self.assertRaises(InvalidImage):
            install_inputs(dest, b"invented", {}, [], self.root / "own.iso")
        self.assertEqual(target.read_bytes(), b"invented")


if __name__ == "__main__":
    unittest.main()
