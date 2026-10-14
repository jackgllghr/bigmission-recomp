"""English compact encoding, pagination and human import with synthetic data."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import unicodedata

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from dialogue_assets import GlyphMap, decode_message, encode_english
from dialogue_drafts import dialogue_records, import_dialogue, seed_dialogue, validated_pages, wrap_page
from extract_dialogue import csv_text, extract
from test_dialogue_extraction import fixture


class DialogueDraftTests(unittest.TestCase):
    def test_english_uses_font_indices_and_preserves_native_icons(self):
        glyphs = GlyphMap('試験'.encode('cp932'))
        encoded = encode_english("Ab 09! Don't {BUTTON_CROSS}\n{BUTTON_TRIANGLE}", glyphs) + b'\0'
        self.assertEqual(encoded[0], 159)  # Latin A glyph, not ASCII 65
        message = decode_message(encoded, 0, len(encoded), glyphs)
        text = unicodedata.normalize('NFKC', message.pages[0])
        self.assertEqual(text, "Ab 09! Don’t {BUTTON_CROSS}\n{BUTTON_TRIANGLE}")
        with self.assertRaises(ValueError):
            encode_english('Café', glyphs)

    def test_pagination_measures_icons_as_one_cell_and_does_not_truncate(self):
        pages = wrap_page('Press {BUTTON_TRIANGLE} to jump, then walk forward and try again.', columns=10, lines=2)
        self.assertGreater(len(pages), 1)
        self.assertEqual(' '.join(page.replace('\n', ' ') for page in pages),
                         'Press {BUTTON_TRIANGLE} to jump, then walk forward and try again.')
        with self.assertRaises(ValueError):
            wrap_page('x' * 21)
        with self.assertRaises(ValueError):
            wrap_page('Text', columns=21)

    def test_identity_controls_complete_messages_and_source_preservation(self):
        elf, metadata, _ = fixture()
        _, rows, _ = extract(elf, metadata)
        drafts = {'schema': 1, 'locale': 'en', 'layout': {'columns': 20, 'lines': 3},
                  'pages': [{'id': row['key'], 'source_sha256': row['source_sha256'],
                             'text': 'Press {BUTTON_TRIANGLE}.' if row['page_index'] == '0' and row['message_id'] != '3'
                                     else '{CONTROL_FD}' if row['page_index'] == '1' else 'Text',
                             'review_status': 'draft', 'origin': 'machine_translation'} for row in rows]}
        before = bytes(elf)
        payload, report, count = dialogue_records(elf, drafts, metadata)
        self.assertEqual(count, 3)
        self.assertEqual(struct.unpack_from('<H', payload)[0], 3)
        self.assertEqual(bytes(elf), before)
        for change in ({'source_sha256': '0' * 64}, {'text': 'Lost icon'}):
            bad = copy.deepcopy(drafts); bad['pages'][0].update(change)
            with self.assertRaises(ValueError):
                validated_pages(bad, rows)
        bad = copy.deepcopy(drafts); bad['pages'].pop(0)
        with self.assertRaisesRegex(ValueError, 'every page'):
            dialogue_records(elf, bad, metadata)
        bad = copy.deepcopy(drafts)
        bad['pages'][0]['text'] = 'Press {BUTTON_TRIANGLE}, then continue walking toward the far side of the room and examine the nearby wall carefully.'
        with self.assertRaisesRegex(ValueError, 'voice/page alignment'):
            dialogue_records(elf, bad, metadata)
        bad['pages'][0]['runtime_text'] = 'Press {BUTTON_TRIANGLE}. Check wall.'
        _, shortened_report, _ = dialogue_records(elf, bad, metadata)
        self.assertEqual(len(shortened_report[0]['runtime_pages']), 1)
        seed_dialogue(rows, drafts)
        snapshot = copy.deepcopy(rows)
        rows[0].update(translation_en='Use {BUTTON_TRIANGLE}.', review_status='reviewed', translator_note='Human review')
        seed_dialogue(rows, drafts)
        self.assertEqual(rows[0]['translation_en'], 'Use {BUTTON_TRIANGLE}.')
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            (path / 'catalog.json').write_text(json.dumps({'pages': snapshot}))
            (path / 'dialogue.csv').write_text(csv_text(rows))
            imported = import_dialogue(path, drafts)
            self.assertEqual(imported['pages'][0]['origin'], 'human_translation')
            self.assertEqual(imported['pages'][0]['text'], 'Use {BUTTON_TRIANGLE}.')
            rows[0].update(translation_en='', review_status='untranslated')
            (path / 'dialogue.csv').write_text(csv_text(rows))
            omitted = import_dialogue(path, drafts)
            self.assertTrue(all('message_001' not in e['id'] for e in omitted['pages']))


if __name__ == '__main__':
    unittest.main()
