"""Synthetic local-handoff fixtures; no executable/disc is required."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from extract_translation_labels import (csv_text, extract, placeholders, read_edits,
                                        rodata_range)


def fixture():
    elf = bytearray(2048)
    elf[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<I', elf, 32, 64)
    struct.pack_into('<3H', elf, 46, 40, 3, 1)
    names = b'\0.shstrtab\0.rodata\0'
    elf[200:200 + len(names)] = names
    struct.pack_into('<10I', elf, 104, 1, 3, 0, 0, 200, len(names), 0, 0, 1, 0)
    struct.pack_into('<10I', elf, 144, 11, 1, 2, 0, 512, 1536, 0, 0, 4, 0)

    def string(offset, text):
        data = text.encode('cp932') + b'\0'
        elf[offset:offset + len(data)] = data

    # Identical labels in two contexts must remain separate occurrences.
    string(520, '試験の島')
    struct.pack_into('<2I', elf, 648, 0, 29)
    string(660, '試験の島')
    struct.pack_into('<3I', elf, 788, 2, 0, 1)
    string(804, '試験キャラ')
    struct.pack_into('<2I', elf, 932, 0, 1)
    struct.pack_into('<5I', elf, 944, 1, 0, 968 - 0x74, 3, 0)
    string(968, '試験の部屋')
    string(1100, '未確認の説明\n%s 個')
    string(1200, '試験 %02d\n説明')
    elf[1300:1306] = b'\x82\0\xff\x01\x02\0'  # never replacement-decode invalid data
    tables = {
        'level_names': {'offset': '0x208', 'stride': 136, 'count': 1, 'name_bytes': 128},
        'mission_names': {'offset': '0x294', 'stride': 140, 'count': 1, 'name_bytes': 128},
        'entity_names': {'offset': '0x324', 'stride': 136, 'count': 1, 'name_bytes': 128},
        'facility_names': {'offset': '0x208', 'stride': 136, 'count': 0, 'name_bytes': 128},
        'area_names': {'offset': '0x3b0', 'stride': 20, 'count': 1},
        'characters': {'0': 'test_character'},
    }
    menu = {'strings': [{'id': 'save.test', 'offset': 1200, 'text': 'Test %02d\nDetails',
                          'max_columns': 44, 'max_lines': 5}]}
    return elf, tables, menu


class TranslationLabelExtractionTests(unittest.TestCase):
    def test_context_duplicate_occurrences_and_unknown_speaker(self):
        elf, tables, menu = fixture()
        labels, candidates = extract(elf, tables, menu)
        rows = {row['key']: row for row in labels}
        self.assertEqual(len(rows), 5)
        area = rows['level.stage_00.area_001.name']
        self.assertEqual(area['level_name_ja'], '試験の島')
        self.assertEqual(area['area_id'], 'area_001')
        self.assertEqual(rows['character.test_character.name']['character_name_ja'], '試験キャラ')
        self.assertTrue(all(row['speaker_id'] == '' for row in labels + candidates))
        self.assertEqual(rows['level.stage_00.name']['duplicate_group'],
                         rows['level.stage_00.mission_00.name']['duplicate_group'])
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0]['context_status'], 'unresolved')
        self.assertEqual((labels, candidates), extract(elf, tables, menu))

    def test_monkey_names_follow_record_ids_and_validated_pointers(self):
        elf, tables, menu = fixture()
        tables['monkey_names'] = {'offset': '0x578', 'stride': 40, 'count': 1, 'pointer_offset': 8}
        struct.pack_into('<3I', elf, 1400, 7, 1, 1500 - 0x74)
        raw = 'テストサル'.encode('cp932') + b'\0'
        elf[1500:1500 + len(raw)] = raw
        labels, _ = extract(elf, tables, menu)
        row = next(row for row in labels if row['key'] == 'monkey.record_007.name')
        self.assertEqual(row['source_ja'], 'テストサル')
        self.assertEqual(row['kind'], 'monkey_name')
        self.assertEqual(row['source_offset'], '0x000005dc')
        struct.pack_into('<I', elf, 1408, 0xffffffff)
        with self.assertRaises(ValueError):
            extract(elf, tables, menu)

    def test_elf_bounds_and_area_pointer_validation(self):
        elf, tables, menu = fixture()
        for field, value in [(32, 99999), (144 + 8, 6), (144 + 20, 99999)]:
            bad = bytearray(elf)
            struct.pack_into('<I', bad, field, value)
            with self.assertRaises(ValueError):
                rodata_range(bad)
        struct.pack_into('<I', elf, 944 + 8, 0xffffffff)
        with self.assertRaises(ValueError):
            extract(elf, tables, menu)

    def test_conflicting_area_context_is_not_silently_assigned(self):
        elf, tables, menu = fixture()
        struct.pack_into('<I', elf, 944 + 16, 3)
        labels, _ = extract(elf, tables, menu)
        row = next(row for row in labels if row['kind'] == 'area_name')
        self.assertEqual(row['key'], 'level.unresolved.area_001.name')
        self.assertEqual(row['level_id'], '')
        self.assertEqual(row['context_status'], 'ambiguous_table_association')
        self.assertEqual([c['id'] for c in json.loads(row['level_candidates'])], ['stage_00', 'stage_03'])

    def test_csv_roundtrip_preserves_multiline_edits_and_rejects_drift(self):
        elf, tables, menu = fixture()
        labels, _ = extract(elf, tables, menu)
        edited = [dict(row) for row in labels]
        edited[0].update(translation_en='Test, "quoted"\nname', review_status='draft', translator_note='Check scene')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'labels.csv'
            path.write_text(csv_text(edited), encoding='utf-8', newline='')
            self.assertEqual(read_edits(path, labels), edited)
            edited[0]['level_id'] = 'stage_99'
            path.write_text(csv_text(edited), encoding='utf-8', newline='')
            with self.assertRaisesRegex(ValueError, 'source/context changed'):
                read_edits(path, labels)
            path.write_text(csv_text(labels[:-1]), encoding='utf-8', newline='')
            with self.assertRaisesRegex(ValueError, 'missing rows'):
                read_edits(path, labels)
            path.write_text(csv_text(labels + [labels[0]]), encoding='utf-8', newline='')
            with self.assertRaisesRegex(ValueError, 'unknown/duplicate'):
                read_edits(path, labels)

    def test_tokens_and_translation_validation(self):
        self.assertEqual(placeholders('%s %02d %% %1$u'), ['%s', '%02d', '%%', '%1$u'])
        with self.assertRaises(ValueError):
            placeholders('Unknown %Q')
        elf, tables, menu = fixture()
        labels, _ = extract(elf, tables, menu)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'labels.csv'
            for text, status in [('Wrong %s', 'draft'), ('', 'reviewed'), ('Bad\t%02d', 'draft')]:
                edited = [dict(row) for row in labels]
                row = next(row for row in edited if row['key'] == 'save.test')
                row.update(translation_en=text, review_status=status)
                path.write_text(csv_text(edited), encoding='utf-8', newline='')
                with self.assertRaises(ValueError):
                    read_edits(path, labels)


if __name__ == '__main__':
    unittest.main()
