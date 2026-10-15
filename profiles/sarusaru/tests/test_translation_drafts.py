"""Draft handoff and runtime catalog integration using invented labels."""
import copy
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from extract_translation_labels import csv_text, extract
from translation_drafts import import_handoff, merge_gameplay_catalog, seed_drafts, validated_drafts
from test_translation_label_extraction import fixture
from translation_catalogs import catalog


class TranslationDraftTests(unittest.TestCase):
    def setUp(self):
        self.elf, self.metadata, self.menu = fixture()
        self.rows, _ = extract(self.elf, self.metadata, self.menu)
        self.drafts = {'schema': 1, 'locale': 'en', 'strings': [
            {'id': row['key'], 'source_sha256': row['source_sha256'],
             'text': 'Machine draft', 'review_status': 'draft', 'origin': 'machine_translation'}
            for row in self.rows if row['kind'] != 'dialog']}

    def test_only_fixed_name_tables_receive_padding_capacity(self):
        merged = merge_gameplay_catalog(self.elf, self.menu, self.drafts, self.metadata)
        rows = {entry['id']: entry for entry in merged['strings']}
        self.assertEqual(rows['character.test_character.name']['capacity'], 128)
        self.assertNotIn('capacity', rows['level.stage_00.area_001.name'])
        self.assertNotIn('capacity', rows['save.test'])
        self.assertNotIn('capacity', self.menu['strings'][0])

    def test_compact_capture_names_match_text_even_when_table_order_differs(self):
        from dialogue_assets import GlyphMap, encoded_glyph, encode_english
        from dialogue_drafts import compact_name_records
        glyphs = GlyphMap('試験'.encode('cp932'))
        inverse = {glyphs.character(i): i for i in range(380)}
        self.metadata['monkey_names'] = {'offset': '0x578', 'stride': 40, 'count': 2, 'pointer_offset': 8}
        self.metadata['compact_monkey_names'] = {'offset': '0x708', 'count': 2}
        for index, (name, address) in enumerate([('テストサル', 1500), ('さる', 1540)]):
            struct.pack_into('<3I', self.elf, 1400 + index * 40, 7 + index, 1, address - 0x74)
            raw = name.encode('cp932') + b'\0'
            self.elf[address:address + len(raw)] = raw
        for index, name in enumerate(['さる', 'テストサル']):
            address = 1600 + index * 20
            raw = b''.join(encoded_glyph(inverse[ch]) for ch in name) + b'\0'
            self.elf[address:address + len(raw)] = raw
            struct.pack_into('<I', self.elf, 1800 + index * 4, address - 0x74)
        rows, _ = extract(self.elf, self.metadata, self.menu)
        drafts = {'schema': 1, 'locale': 'en', 'strings': [
            {'id': row['key'], 'source_sha256': row['source_sha256'], 'text': text,
             'review_status': 'draft'} for row, text in
            zip([r for r in rows if r['kind'] == 'monkey_name'], ['Alpha', 'Beta'])]}
        records, count = compact_name_records(self.elf, drafts, self.metadata, glyphs)
        self.assertEqual(count, 2)
        self.assertIn(b'monkey.record_008.name.compact', records[:100])
        self.assertIn(encode_english('Beta', glyphs) + b'\0', records)
        self.assertIn(encode_english('Alpha', glyphs) + b'\0', records)
        # An omitted translation leaves that compact lookup untouched.
        drafts['strings'].pop()
        self.assertEqual(compact_name_records(self.elf, drafts, self.metadata, glyphs)[1], 1)
        struct.pack_into('<I', self.elf, 1800, 0xffffffff)
        with self.assertRaises(ValueError):
            compact_name_records(self.elf, drafts, self.metadata, glyphs)

    def test_ui_captions_validate_source_lookup_and_panel_budget(self):
        import hashlib
        from dialogue_assets import GlyphMap, encoded_glyph, encode_english
        from dialogue_drafts import ui_caption_records
        glyphs = GlyphMap('試験'.encode('cp932'))
        inverse = {glyphs.character(i): i for i in range(380)}
        source = b''.join(encoded_glyph(inverse[ch]) for ch in 'もどる') + b'\0'
        self.elf[1600:1600 + len(source)] = source
        struct.pack_into('<I', self.elf, 1800, 1600 - 0x74)
        entry = {'id': 'pause.synthetic', 'source_offset': 1600, 'pointer_offset': 1800,
                 'source_sha256': hashlib.sha256(source).hexdigest(), 'text': 'Resume.',
                 'columns': 14, 'lines': 1, 'review_status': 'draft'}
        menu = {'compact_ui': [entry]}
        records, count = ui_caption_records(self.elf, menu, glyphs)
        self.assertEqual(count, 1)
        self.assertIn(encode_english('Resume.', glyphs) + b'\0', records)
        for changes in [{'source_sha256': '0' * 64}, {'pointer_offset': 1804},
                        {'text': 'This caption needs far too many lines.'},
                        {'text': '{BUTTON_CIRCLE}'}, {'text': ''}]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                ui_caption_records(self.elf, {'compact_ui': [dict(entry, **changes)]}, glyphs)
        self.assertEqual(ui_caption_records(b'', {}), (b'\0\0', 0))

    def test_machine_defaults_do_not_overwrite_human_work(self):
        self.rows[0].update(translation_en='Human wording', review_status='reviewed', translator_note='Approved terminology')
        seed_drafts(self.rows, self.drafts)
        self.assertEqual(self.rows[0]['translation_en'], 'Human wording')
        self.assertEqual(self.rows[0]['review_status'], 'reviewed')
        machine = next(row for row in self.rows if row['key'] == 'level.stage_00.name')
        self.assertEqual(machine['translation_en'], 'Machine draft')
        self.assertIn('human review pending', machine['translator_note'])

    def test_draft_identity_format_and_placeholder_checks(self):
        for change in ({'source_sha256': '0' * 64}, {'id': 'unresolved.unknown'},
                       {'text': 'Unexpected %s'}, {'text': 'Non-ASCII \u00e9'},
                       {'review_status': 'untranslated'}):
            draft = copy.deepcopy(self.drafts)
            draft['strings'][0].update(change)
            with self.assertRaises(ValueError):
                validated_drafts(draft, self.rows)
        duplicate = copy.deepcopy(self.drafts)
        duplicate['strings'].append(duplicate['strings'][0])
        with self.assertRaises(ValueError):
            validated_drafts(duplicate, self.rows)

    def test_short_runtime_variant_does_not_replace_full_handoff_text(self):
        entry = next(e for e in self.drafts['strings'] if '.area_' in e['id'])
        entry['runtime_text'] = 'Short name'
        seed_drafts(self.rows, self.drafts)
        row = next(r for r in self.rows if r['key'] == entry['id'])
        self.assertEqual(row['translation_en'], 'Machine draft')
        self.assertIn('Short name', row['translator_note'])
        merged = merge_gameplay_catalog(self.elf, self.menu, self.drafts, self.metadata)
        self.assertEqual(next(e for e in merged['strings'] if e['id'] == entry['id'])['text'], 'Short name')

    # The catalog is in the separate sarusaru-translations repository.
    @unittest.skipUnless(catalog('english-gameplay-v1.json').is_file(), 'sarusaru-translations is not checked out')
    def test_v1_has_complete_unique_english_authoring_entries(self):
        catalog_json = json.loads(catalog('english-gameplay-v1.json').read_text())
        entries = catalog_json['strings']
        self.assertEqual(len(entries), 400)
        self.assertEqual(len({entry['id'] for entry in entries}), 400)
        self.assertEqual(sum(e['id'].startswith('monkey.') for e in entries), 207)
        self.assertTrue(all(e['origin'] in ('machine_translation', 'human_translation') and
                            e['review_status'] in ('draft', 'reviewed') for e in entries))
        self.assertTrue(all(not e['id'].startswith('unresolved.boot.') for e in entries))
        for entry in entries:
            self.assertEqual(len(entry['source_sha256']), 64)
            int(entry['source_sha256'], 16)
            for field in ('text', 'runtime_text'):
                value = entry.get(field, entry['text'])
                self.assertTrue(value and all(32 <= ord(c) <= 126 for c in value))

    def test_human_import_preserves_identity_and_discards_stale_runtime_wording(self):
        area = next(e for e in self.drafts['strings'] if '.area_' in e['id'])
        area['runtime_text'] = 'Old short'
        seed_drafts(self.rows, self.drafts)
        snapshot = copy.deepcopy(self.rows)
        row = next(r for r in self.rows if r['key'] == area['id'])
        row.update(translation_en='Human room', review_status='reviewed', translator_note='Checked on screen')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            (path / 'catalog.json').write_text(json.dumps({'labels': snapshot}))
            (path / 'labels.csv').write_text(csv_text(self.rows), encoding='utf-8', newline='')
            imported = import_handoff(path, self.drafts)
        changed = next(e for e in imported['strings'] if e['id'] == area['id'])
        self.assertEqual(changed['text'], 'Human room')
        self.assertEqual(changed['source_sha256'], area['source_sha256'])
        self.assertEqual(changed['origin'], 'human_translation')
        self.assertEqual(changed['review_status'], 'reviewed')
        self.assertNotIn('runtime_text', changed)
        self.assertIn('runtime_text', area)


if __name__ == '__main__':
    unittest.main()
