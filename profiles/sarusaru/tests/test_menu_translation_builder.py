"""Catalog layout checks using synthetic canvases (optional Pillow tooling)."""
import copy
import importlib.util
from pathlib import Path
import sys
import unittest

PROFILE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROFILE / 'tools'))
from build_menu_translation import ASSETS, build, render_labels, merge_regions


def synthetic_catalog():
    """Invented labels in the five required title/main-menu slots."""
    def label(key, text, asset, rect, size):
        return {'id': key, 'text': text, 'asset': asset, 'rect': rect, 'font_size': size}
    return {'schema': 1, 'locale': 'en', 'labels': [
        label('title.press_start', 'Push a button', 'e_nabi_a0.lzs', [0, 0, 256, 16], 13),
        label('menu.new_game', 'First', 'b_but_b0.lzs', [0, 0, 256, 24], 18),
        label('menu.continue', 'Second', 'b_but_b0.lzs', [0, 24, 256, 24], 18),
        label('menu.options', 'Third', 'b_but_b0.lzs', [0, 48, 256, 24], 18),
        label('menu.gachi_king', 'Fourth', 'b_but_b0.lzs', [0, 72, 256, 24], 18),
    ]}


class RegionTests(unittest.TestCase):
    def test_merges_adjacent_slots_but_preserves_gaps_and_other_columns(self):
        self.assertEqual(merge_regions([[0, 16, 64, 16], [64, 0, 64, 16],
                                       [0, 0, 64, 16], [0, 48, 64, 16]]),
                         [[0, 0, 64, 32], [0, 48, 64, 16], [64, 0, 64, 16]])

    def test_full_action_atlas_stays_within_runtime_region_limit(self):
        # More than 64 action slots; adjacent rows share replacement rectangles.
        regions = [[x * 64, y * 16, 64, 16] for x in range(3) for y in range(32)]
        self.assertEqual(merge_regions(regions), [[0, 0, 64, 512], [64, 0, 64, 512], [128, 0, 64, 512]])


class OutputSafetyTests(unittest.TestCase):
    def test_output_cannot_replace_a_source_input(self):
        for output in [Path('synthetic.iso'), Path('logo.png'), Path('catalog.json')]:
            with self.assertRaises(ValueError):
                build(Path('synthetic.iso'), Path('logo.png'), Path('catalog.json'), output)


@unittest.skipUnless(importlib.util.find_spec('PIL'), 'Pillow is required for translation authoring tests')
class MenuCatalogTests(unittest.TestCase):
    def setUp(self):
        from PIL import Image
        self.catalog = synthetic_catalog()
        self.images = {asset: Image.new('RGBA', size) for asset, (_, size, _) in ASSETS.items()}

    def test_catalog_renders_all_labels_within_original_slots(self):
        render_labels(self.catalog, self.images)
        for y in range(0, 96, 24):
            self.assertIsNotNone(self.images['b_but_b0.lzs'].crop((0, y, 256, y + 24)).getbbox())
        self.assertIsNotNone(self.images['e_nabi_a0.lzs'].getbbox())

    def test_rejects_missing_labels_overlap_overflow_and_uncovered_glyphs(self):
        variants = []
        missing = copy.deepcopy(self.catalog); missing['labels'].pop(1); variants.append(missing)
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
