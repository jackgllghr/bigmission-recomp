"""Invented compact messages, glyph ordering and ELF; no game data required."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from dialogue_assets import GlyphMap, decode_message, original_bytes, pointer_file_offset
from extract_dialogue import DIALOGUE_FIELDS, csv_text, extract, protected_tokens
from extract_translation_labels import read_edits


def fixture():
    elf = bytearray(2048)
    elf[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<I', elf, 28, 224)
    struct.pack_into('<I', elf, 32, 64)
    struct.pack_into('<2H', elf, 42, 32, 1)
    struct.pack_into('<3H', elf, 46, 40, 4, 1)
    names = b'\0.shstrtab\0.rodata\0.data\0'
    elf[256:256 + len(names)] = names
    struct.pack_into('<10I', elf, 104, 1, 3, 0, 0, 256, len(names), 0, 0, 1, 0)
    struct.pack_into('<10I', elf, 144, 11, 1, 2, 0x2000, 512, 1024, 0, 0, 4, 0)
    struct.pack_into('<10I', elf, 184, 19, 1, 3, 0x2400, 1536, 512, 0, 0, 4, 0)
    struct.pack_into('<8I', elf, 224, 1, 512, 0x2000, 0, 1536, 1536, 6, 4)
    elf[520:522] = b'\x01\0'  # whitespace-only placeholder
    raw = b'\xd4\xf0\x00\xf0\x8d\xfe\x63\xff\xf0\x8e\xfd\0'
    elf[540:540 + len(raw)] = raw
    elf[580:583] = b'\xf0\0\0'  # escape trail NUL must survive
    kanji = '試験'.encode('cp932')
    elf[1400:1404] = kanji
    for i, offset in enumerate([520, 540, 540, 580]):
        struct.pack_into('<I', elf, 1600 + i * 4, offset - 512 + 0x2000)
        struct.pack_into('<I', elf, 1450 + i * 4, 10 + i)
    metadata = {'pointer_table': '0x640', 'message_count': 4,
                'message_region': ['0x200', '0x400'], 'kanji_table': '0x578',
                'kanji_bytes': 4, 'auxiliary_table': '0x5aa',
                'topics': {'1': 'test_controls', '2': 'different_context'}}
    return elf, metadata, raw


class DialogueExtractionTests(unittest.TestCase):
    def test_embedded_nul_pages_icons_controls_and_lossless_tokens(self):
        _, _, raw = fixture()
        message = decode_message(raw, 0, len(raw), GlyphMap('試験'.encode('cp932')))
        self.assertEqual(message.pages, ['あぞ試\n{BUTTON_TRIANGLE}', '験{CONTROL_FD}'])
        self.assertEqual(message.end, len(raw))
        self.assertEqual(original_bytes(message), raw)
        self.assertEqual(message.page_spans, [(0, 7), (8, 11)])
        unknown = GlyphMap('試験'.encode('cp932')).character(63)
        self.assertEqual(unknown, '{GLYPH_003F}')

    def test_table_ids_aliases_pages_and_context_are_not_conflated(self):
        elf, metadata, _ = fixture()
        messages, rows, blanks = extract(elf, metadata, {'test_character': '試'})
        self.assertEqual(len(messages), 3)
        self.assertEqual(len(rows), 5)
        self.assertEqual(len(blanks), 1)
        self.assertEqual(blanks[0]['message_id'], 0)
        self.assertEqual(messages[0]['source_sha256'], messages[1]['source_sha256'])
        self.assertNotEqual(messages[0]['key'], messages[1]['key'])
        self.assertEqual(rows[0]['message_id'], '1')
        self.assertEqual(rows[0]['page_index'], '0')
        self.assertIn('test_character', json.loads(rows[0]['mentioned_characters']))
        self.assertTrue(all(not row['speaker_id'] and not row['level_id'] for row in rows))
        self.assertEqual(rows[-1]['source_ja'], 'ぞ')
        self.assertEqual((messages, rows, blanks), extract(elf, metadata, {'test_character': '試'}))

    def test_truncation_unmapped_pointers_overlapping_messages_and_bounds(self):
        glyphs = GlyphMap('試験'.encode('cp932'))
        for raw in [b'\xf0', b'\xf3\xff\0', b'\xd4']:
            with self.assertRaises(ValueError):
                decode_message(raw, 0, len(raw), glyphs)
        with self.assertRaises(ValueError):
            decode_message(b'\xd4\0', 0, 2, glyphs, limit=1)
        elf, metadata, _ = fixture()
        with self.assertRaises(ValueError):
            pointer_file_offset(elf, 0xffffffff)
        for pointer in [0xffffffff, 1400 - 512 + 0x2000, 544 - 512 + 0x2000]:
            bad = bytearray(elf)
            struct.pack_into('<I', bad, 1608, pointer)
            with self.assertRaises(ValueError):
                extract(bad, metadata)
        bad_metadata = copy.deepcopy(metadata)
        bad_metadata['kanji_bytes'] = 10000
        with self.assertRaises(ValueError):
            extract(elf, bad_metadata)

    def test_csv_preserves_edits_and_checks_tokens_and_source_context(self):
        elf, metadata, _ = fixture()
        _, originals, _ = extract(elf, metadata)
        edited = copy.deepcopy(originals)
        edited[0].update(translation_en='Press {BUTTON_TRIANGLE}.\nContinue, "please".',
                         review_status='draft', translator_note='Speaker needs capture')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'dialogue.csv'
            path.write_text(csv_text(edited), encoding='utf-8')
            self.assertEqual(read_edits(path, originals, DIALOGUE_FIELDS, protected_tokens), edited)
            for text in ['Lost button', '{BUTTON_CIRCLE}', '{BUTTON_TRIANGLE} {BUTTON_TRIANGLE}',
                         '{BUTTON_TRIANGLE} {BOGUS}', '{BUTTON_TRIANGLE']:
                bad = copy.deepcopy(edited)
                bad[0]['translation_en'] = text
                path.write_text(csv_text(bad), encoding='utf-8')
                with self.assertRaises(ValueError):
                    read_edits(path, originals, DIALOGUE_FIELDS, protected_tokens)
            bad = copy.deepcopy(edited)
            bad[0]['speaker_id'] = 'guessed_speaker'
            path.write_text(csv_text(bad), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'source/context changed'):
                read_edits(path, originals, DIALOGUE_FIELDS, protected_tokens)
            path.write_text(csv_text(edited[:-1]), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'missing rows'):
                read_edits(path, originals, DIALOGUE_FIELDS, protected_tokens)

    def test_protected_mentions_can_reorder_without_changing_identity(self):
        self.assertEqual(protected_tokens('{BUTTON_CROSS} then {BUTTON_SQUARE}'),
                         protected_tokens('{BUTTON_SQUARE} before {BUTTON_CROSS}'))
        with self.assertRaises(ValueError):
            protected_tokens('{BUTTON_UNKNOWN}')


if __name__ == '__main__':
    unittest.main()
