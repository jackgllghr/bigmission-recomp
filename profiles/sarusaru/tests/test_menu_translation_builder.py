"""Catalog layout checks using synthetic canvases (optional Pillow tooling)."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

PROFILE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROFILE / 'tools'))
from build_menu_translation import build, render_labels


class OutputSafetyTests(unittest.TestCase):
    def test_output_cannot_replace_a_source_input(self):
        for output in [Path('synthetic.iso'), Path('logo.png'), Path('catalog.json')]:
            with self.assertRaises(ValueError):
                build(Path('synthetic.iso'), Path('logo.png'), Path('catalog.json'), output)


@unittest.skipUnless(importlib.util.find_spec('PIL'), 'Pillow is required for translation authoring tests')
class MenuCatalogTests(unittest.TestCase):
    def setUp(self):
        from PIL import Image
        self.catalog = json.loads((PROFILE / 'resources/translations/english-menu.json').read_text())
        self.images = {'b_but_b0.lzs': Image.new('RGBA', (256, 96)),
                       'e_nabi_a0.lzs': Image.new('RGBA', (256, 16))}

    def test_default_catalog_renders_all_labels_within_original_slots(self):
        render_labels(self.catalog, self.images)
        for y in range(0, 96, 24):
            self.assertIsNotNone(self.images['b_but_b0.lzs'].crop((0, y, 256, y + 24)).getbbox())
        self.assertIsNotNone(self.images['e_nabi_a0.lzs'].getbbox())

    def test_rejects_missing_labels_overlap_overflow_and_uncovered_glyphs(self):
        variants = []
        missing = copy.deepcopy(self.catalog); missing['labels'].pop(); variants.append(missing)
        duplicate = copy.deepcopy(self.catalog); duplicate['labels'].append(duplicate['labels'][0]); variants.append(duplicate)
        overlap = copy.deepcopy(self.catalog); overlap['labels'][2]['rect'] = [0, 0, 256, 24]; variants.append(overlap)
        overflow = copy.deepcopy(self.catalog); overflow['labels'][1]['text'] = 'A' * 100; variants.append(overflow)
        glyph = copy.deepcopy(self.catalog); glyph['labels'][1]['text'] = 'Café'; variants.append(glyph)
        bounds = copy.deepcopy(self.catalog); bounds['labels'][1]['rect'] = [-1, 0, 256, 24]; variants.append(bounds)
        for variant in variants:
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                render_labels(variant, self.images)


if __name__ == '__main__':
    unittest.main()
