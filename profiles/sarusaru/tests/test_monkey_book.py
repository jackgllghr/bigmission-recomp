"""Monkey Book glyph runs from a synthetic executable (optional Pillow for rendering)."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

PROFILE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROFILE / 'tools'))
import monkey_book
from dialogue_assets import RANGES, GlyphMap

RECORDS, STRINGS = 0x100, 0x400
FIELDS = {'description': 12, 'kind': 16, 'square': 24, 'circle': 28, 'cross': 32}
UI = {'actions': '□：%s\n○：%s\n×：%s\n', 'stage': 'ステージ%s', 'caught': '%sかいのった',
      'caught_zero': '　　０かいのった', 'unknown': '？？？'}


def index(char, kanji=''):
    code = int.from_bytes(char.encode('cp932'), 'big')
    for lo, hi, start in RANGES:
        if start <= code < start + hi - lo:
            return lo + code - start
    return 380 + kanji.index(char)


def synthetic(records):
    """Records of (id, {field: text}); every string gets its own slot."""
    boot = bytearray(0x1000)
    cursor = STRINGS
    ui = {}

    def put(text):
        nonlocal cursor
        offset = cursor
        data = text.encode('cp932') + b'\0'
        boot[offset:offset + len(data)] = data
        cursor += len(data) + 1
        return offset
    for i, (ident, fields) in enumerate(records):
        struct.pack_into('<I', boot, RECORDS + i * 40, ident)
        for field, at in FIELDS.items():
            struct.pack_into('<I', boot, RECORDS + i * 40 + at, put(fields.get(field, '')))
    for key, text in UI.items():
        ui[key] = hex(put(text))
    metadata = {'monkey_names': {'offset': hex(RECORDS), 'stride': 40, 'count': len(records)},
                'monkey_book': {'pointer_bias': 0, 'fields': FIELDS, 'ui': ui}}
    return bytes(boot), metadata


def entry(key, source, text):
    return {'id': key, 'source_sha256': hashlib.sha256(source.encode('cp932') + b'\0').hexdigest(),
            'text': text, 'review_status': 'draft'}


def signature(indices):
    return hashlib.sha256(b''.join(struct.pack('<3H', column * 16, (i % 32) * 16, (i // 32) * 16)
                                   for column, i in indices)).hexdigest()


class MonkeyBookTests(unittest.TestCase):
    def setUp(self):
        self.kanji = '的'
        self.glyphs = GlyphMap(self.kanji.encode('cp932'))
        self.boot, self.metadata = synthetic([
            (32, {'description': '的に　あたる', 'kind': 'とかげ', 'square': 'ゆみ', 'circle': 'ゆみ', 'cross': 'ジャンプ'}),
            (33, {'description': 'ほか', 'kind': 'とかげ', 'square': 'ゆみ', 'circle': '', 'cross': 'ジャンプ'}),
        ])

    def runs(self, entries):
        return {run['id']: run for run in monkey_book.runs(self.boot, {'monkey_book': entries}, self.glyphs, self.metadata)}

    def test_shared_text_is_one_entry_named_after_its_first_record(self):
        sources = {row['id']: row for row in monkey_book.sources(self.boot, self.metadata)}
        self.assertIn('monkey.record_032.kind', sources)
        self.assertNotIn('monkey.record_033.kind', sources)
        self.assertIn('monkey.record_033.description', sources)
        self.assertNotIn('monkey.record_033.circle', sources)  # Empty actions draw nothing.

    def test_record_lines_match_the_guest_cells_including_drawn_spaces(self):
        runs = self.runs([entry('monkey.record_032.kind', 'とかげ', 'Lizard'),
                          entry('monkey.record_032.description', '的に　あたる', 'Never hits')])
        kind = runs['monkey.record_032.kind.native']
        self.assertEqual(kind['signature'], signature(enumerate(index(c) for c in 'とかげ')))
        self.assertEqual((kind['width'], kind['glyph_count'], kind['text']), (136, 3, 'Lizard'))
        description = runs['monkey.record_032.description.native']
        # A string's full-width space is the blank cell, drawn like any glyph.
        cells = [0 if c == '　' else index(c, self.kanji) for c in '的に　あたる']
        self.assertEqual(description['signature'], signature(enumerate(cells)))
        self.assertEqual(description['glyph_count'], 6)

    def test_actions_include_their_button_and_colon_cells(self):
        runs = self.runs([entry('monkey.record_032.square', 'ゆみ', 'Aim'),
                          entry('monkey.record_032.circle', 'ゆみ', 'Shoot')])
        square, circle = runs['monkey.record_032.square.native'], runs['monkey.record_032.circle.native']
        self.assertEqual(square['signature'], signature(enumerate(index(c) for c in '□：ゆみ')))
        self.assertEqual((square['button'], square['width'], square['glyph_count']), ('square', 232, 4))
        self.assertEqual(circle['button'], 'circle')
        self.assertNotEqual(square['signature'], circle['signature'])

    def test_counter_skips_padding_and_keeps_its_right_edge(self):
        runs = self.runs([entry('monkey_book.caught', UI['caught'], 'Caught {n} times'),
                          entry('monkey_book.caught_once', UI['caught'], 'Caught once')])
        suffix = [index(c) for c in 'かいのった']
        two, twelve = runs['monkey_book.caught.2.native'], runs['monkey_book.caught.12.native']
        self.assertEqual(two['signature'], signature(enumerate([150] + suffix)))
        self.assertEqual((two['text'], two['width'], two['align']), ('Caught 2 times', 96, 'right'))
        self.assertEqual(twelve['signature'], signature(enumerate([149, 150] + suffix)))
        self.assertEqual(twelve['width'], 112)
        self.assertEqual(runs['monkey_book.caught.1.native']['text'], 'Caught once')
        self.assertEqual(len([key for key in runs if key.startswith('monkey_book.caught.')]), 100)

    def test_stage_header_and_unknown_entries(self):
        runs = self.runs([entry('monkey_book.stage', UI['stage'], 'Stage {n}'),
                          entry('monkey_book.unknown', UI['unknown'], '???')])
        stage = runs['monkey_book.stage.06.native']
        self.assertEqual(stage['signature'], signature(enumerate([index(c) for c in 'ステージ'] + [148, 154])))
        self.assertEqual((stage['text'], stage['align']), ('Stage 06', 'center'))
        self.assertEqual(runs['monkey_book.unknown.native']['glyph_count'], 3)
        self.assertEqual(runs['monkey_book.unknown.cross.native']['button'], 'cross')

    def test_rejects_unknown_ids_changed_sources_and_non_ascii(self):
        for bad in (entry('monkey.record_033.kind', 'とかげ', 'Lizard'),
                    entry('monkey.record_032.kind', 'とかげX', 'Lizard'),
                    entry('monkey.record_032.kind', 'とかげ', 'Lézard')):
            with self.assertRaises(ValueError):
                self.runs([bad])
        with self.assertRaises(ValueError):
            self.runs([entry('monkey.record_032.kind', 'とかげ', 'A')] * 2)

    def test_names_use_ascii_cells_and_skip_spaces(self):
        runs = monkey_book.name_runs({'strings': [
            {'id': 'monkey.record_001.name', 'text': 'A B'}, {'id': 'monkey.record_002.name', 'text': 'A B'},
            {'id': 'level.record_001.name', 'text': 'Elsewhere'}]})
        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0]['signature'], hashlib.sha256(struct.pack('<6H', 0, 256, 480, 16, 264, 480)).hexdigest())
        self.assertEqual(runs[0]['width'], 136)


@unittest.skipUnless(importlib.util.find_spec('PIL'), 'Pillow is not installed')
class MonkeyBookRenderingTests(unittest.TestCase):
    def test_button_cell_precedes_the_action(self):
        from PIL import Image
        from build_menu_translation import render_line
        run = {'id': 'x', 'text': 'Aim', 'width': 232, 'glyph_count': 4, 'signature': '0' * 64, 'button': 'square'}
        for scale in (1, 4):
            canvas = Image.new('RGBA', (232 * scale, 16 * scale))
            render_line(run, canvas, output_scale=scale)
            button = canvas.crop((0, 0, 16 * scale, 16 * scale)).getbbox()
            self.assertIsNotNone(button)
            self.assertLessEqual(button[2], 16 * scale)
            self.assertIsNotNone(canvas.crop((18 * scale, 0, 232 * scale, 16 * scale)).getbbox())
        # The square's stroke is white with a black edge.
        canvas = Image.new('RGBA', (232 * 4, 64))
        render_line(run, canvas, output_scale=4)
        self.assertEqual(canvas.getpixel((14, 32))[:3], (255, 255, 255))

    def test_table_lines_share_a_baseline(self):
        from build_menu_translation import render_body_text
        tops = [render_body_text(text, 12, 136, 16, output_scale=4, baseline=True).getbbox()[1]
                for text in ('Ukiha', 'Sarupus')]
        self.assertEqual(tops[0], tops[1])


if __name__ == '__main__':
    unittest.main()
