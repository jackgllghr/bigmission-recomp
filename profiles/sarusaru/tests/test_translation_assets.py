"""Invented format fixtures; no disc is required."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from translation_assets import archive_members, decode_lzs, decode_tim2


class TranslationAssetsTests(unittest.TestCase):
    def test_literal_and_overlapping_reference(self):
        # A then a reference into the just-written ring byte, yielding AAAA.
        self.assertEqual(decode_lzs(b'\x04\0\0\0\x01A\xee\xf0'), b'AAAA')
        self.assertEqual(decode_lzs(b'\x03\0\0\0\x07ABC'), b'ABC')

    def test_rejects_truncation_growth_and_trailing_data(self):
        for data in [b'', b'\0' * 4, b'\xff' * 4, b'\x01\0\0\0',
                     b'\x01\0\0\0\0\xee\xf0', b'\x01\0\0\0\1AX']:
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_lzs(data)

    def test_archive_bounds_names_and_ordinals(self):
        record = struct.pack('<4I', 1, 48, 3, 0) + b'invented.bin\0'.ljust(32, b'\0')
        self.assertEqual(archive_members(record + b'ABC'), {'invented.bin': b'ABC'})
        for offset, size, ordinal, name in [(47, 3, 0, 'good'), (48, 4, 0, 'good'),
                                          (48, 3, 1, 'good'), (48, 3, 0, '../escape')]:
            data = struct.pack('<4I', 1, offset, size, ordinal) + name.encode().ljust(32, b'\0') + b'ABC'
            with self.assertRaises(ValueError):
                archive_members(data)

    def test_tim2_swizzle_and_palette_order(self):
        # 32x8 indexed8, physical palette index 16 represents logical index 8.
        header = b'TIM2\x04\0\x01\0' + b'\0' * 8
        picture = struct.pack('<IIIHHBBBBHH', 48 + 256 + 1024, 1024, 256, 48, 256, 0, 1, 3, 5, 32, 8)
        palette = bytearray(1024); palette[16 * 4:17 * 4] = b'RGBA'
        raw = bytearray(256); raw[128] = 8  # second 16-byte-wide block's first pixel
        data = header + picture.ljust(48, b'\0') + raw + palette
        texture = decode_tim2(data)
        self.assertEqual((texture.width, texture.height), (32, 8))
        self.assertEqual(texture.rgba[16 * 4:17 * 4], b'RGBA')
        self.assertEqual(texture.rgba[:4], b'\0' * 4)
        with self.assertRaises(ValueError):
            decode_tim2(data[:-1])


if __name__ == '__main__':
    unittest.main()
