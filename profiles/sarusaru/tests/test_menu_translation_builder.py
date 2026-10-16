"""Catalog layout checks using synthetic canvases (optional Pillow tooling)."""
import copy
import json
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

PROFILE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROFILE / 'tools'))
from build_menu_translation import ASSETS, build, render_labels, merge_regions, render_ui_label, label_font, subtitle_records
from translation_catalogs import catalog


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


class SubtitleRecordTests(unittest.TestCase):
    SRT = b'1\n00:00:01,000 --> 00:00:02,000\nHello\n'

    def records(self, files):
        with tempfile.TemporaryDirectory() as directory:
            for file, data in files.items():
                (Path(directory) / file).write_bytes(data)
            return subtitle_records(Path(directory))

    def test_bundles_each_movie_by_name_and_ignores_other_languages(self):
        data, count = self.records({'opening.en.srt': self.SRT, 'm01.en.srt': self.SRT, 'm01.ja.srt': self.SRT})
        self.assertEqual(count, 2)
        record = struct.pack('<H', 3) + b'm01' + struct.pack('<I', len(self.SRT)) + self.SRT
        self.assertEqual(data[:2], struct.pack('<H', 2))
        self.assertEqual(data[2:2 + len(record)], record)

    def test_rejects_what_the_runtime_would(self):
        for files in [{}, {'M01.en.srt': self.SRT}, {'m01.en.srt': b''}, {'m01.en.srt': b'no timing'},
                      {'m01.en.srt': self.SRT + b'\0'}, {'m01.en.srt': self.SRT + b'a' * 256 * 1024}]:
            with self.assertRaises(ValueError, msg=files):
                self.records(files)
        with self.assertRaises(UnicodeDecodeError):
            self.records({'m01.en.srt': self.SRT + b'\xff'})


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

    @unittest.skipUnless(catalog('english-menu.json').is_file(), 'English catalog is not checked out')
    def test_clear_badges_fit_actual_sample_tiles_and_wobbling_screen_quads(self):
        authored = json.loads(catalog('english-menu.json').read_text())
        images = {asset: image.copy() for asset, image in self.images.items()}
        regions = render_labels(authored, images)
        self.assertEqual(regions['common/e_kaza_0.tm2'], [[0, 0, 96, 80]])
        alpha = images['common/e_kaza_0.tm2'].getchannel('A')
        # Actual guest clear-stamp draws: UV (1,1)..(95,39), with animated rotation.
        # The second stamp samples (1,41)..(95,79) from the same atlas.
        quads = [(417, -4, 411, 35, 512, 9), (417, -7, 407, 31, 510, 15),
                 (416, -1, 414, 38, 512, 4), (416, 1, 417, 41, 512, -1)]
        for top in (0, 40):
            mask = alpha.crop((0, top, 96, top + 40)).point(lambda value: 255 if value >= 32 else 0)
            self.assertIsNotNone(mask.getbbox())
            for y in range(40):
                for x in range(96):
                    if not mask.getpixel((x, y)):
                        continue
                    for tx, ty, bx, by, rx, ry in quads:
                        u, v = (x - 0.5) / 94, (y - 0.5) / 38
                        sx, sy = tx + u * (rx - tx) + v * (bx - tx), ty + u * (ry - ty) + v * (by - ty)
                        self.assertTrue(0 <= sx < 480 and 0 <= sy < 272,
                                        f'stamp {top}: ink at {x},{y} clips screen at {sx},{sy}')

    def test_yellow_ink_is_heavier_and_red_edge_is_offset_down_and_right(self):
        from PIL import Image, ImageDraw, ImageFont
        label = render_ui_label('H', 24, 80, 40)
        regular = Image.new('RGBA', label.size)
        ImageDraw.Draw(regular).text((10, 2), 'H', font=label_font(24), fill=(249, 206, 0, 255))
        def points(image, red=False):
            return [(x, y) for y in range(image.height) for x in range(image.width)
                    if (lambda p: p[3] > 128 and p[0] > 120 and p[2] < 80 and
                        (p[1] < 100 if red else p[0] > 240 and p[1] > 180))(image.getpixel((x, y)))]
        gold, edge = points(label), points(label, True)
        self.assertGreater(len(gold), len(points(regular)))
        for axis in (0, 1):
            self.assertGreaterEqual(max(p[axis] for p in edge) - min(p[axis] for p in edge),
                                    max(p[axis] for p in gold) - min(p[axis] for p in gold) + 2)
            self.assertGreater(sum(p[axis] for p in edge) / len(edge),
                               sum(p[axis] for p in gold) / len(gold))

    def test_small_label_letters_have_extra_separation(self):
        from PIL import Image, ImageDraw, ImageFont
        text = 'IIIIIIII'
        tracked = render_ui_label(text, 10, 100, 20)
        untracked = Image.new('RGBA', (400, 80))
        ImageDraw.Draw(untracked).text((20, 8), text,
            font=label_font(40), fill=(249, 206, 0, 255),
            stroke_width=2, stroke_fill=(249, 206, 0, 255))
        untracked = untracked.resize((100, 20), Image.Resampling.LANCZOS)
        def gold_span(image):
            xs = [x for y in range(image.height) for x in range(image.width)
                  if (lambda p: p[3] > 128 and p[0] > 180 and p[1] > 160
                      and p[2] < 80)(image.getpixel((x, y)))]
            return max(xs) - min(xs) + 1
        # Seven quarter-pixel gaps should remain visible after downsampling.
        self.assertGreaterEqual(gold_span(tracked), gold_span(untracked) + 1)
        self.assertLessEqual(gold_span(tracked), gold_span(untracked) + 3)

    def test_label_fill_and_outline_gradient_match_game_palette(self):
        for style in ({}, {'controls': True}, {'header': True}):
            with self.subTest(style=style):
                image = render_ui_label('H', 24, 80, 40, output_scale=8, **style)
                pixels = set(image.get_flattened_data())
                self.assertIn((249, 206, 0, 255), pixels)
                self.assertNotIn((135, 49, 35, 255), pixels)
                self.assertIn((179, 55, 22, 255), pixels)
                self.assertIn((255, 148, 3, 255), pixels)
                self.assertNotIn((255, 236, 2, 255), pixels)
                # The edge traverses intermediate colors, rather than two
                # flat outline bands. Ignore antialiasing and the white halo.
                gradient = {(r, g, b) for r, g, b, a in pixels
                            if a == 255 and 179 < r < 255 and 55 < g < 148 and 3 < b < 22}
                self.assertGreaterEqual(len(gradient), 8)
        image = render_ui_label('H', 24, 80, 40, controls=True)
        self.catalog['labels'].append({'id': 'control.test', 'text': 'H',
            'asset': 'common00/action_name.tm2', 'rect': [0, 0, 80, 40], 'font_size': 24})
        render_labels(self.catalog, self.images)
        from PIL import Image
        composited = Image.new('RGBA', image.size)
        composited.alpha_composite(image)
        self.assertEqual(self.images['common00/action_name.tm2'].crop((0, 0, 80, 40)).tobytes(),
                         composited.tobytes())

    def test_control_hint_outlines_are_half_the_regular_width(self):
        for scale in (1, 8):
            spans = []
            for controls in (False, True):
                image = render_ui_label('H', 24, 80, 48, controls=controls, output_scale=scale)
                face, border = [], []
                for y in range(40 * scale):  # Exclude the navy footer bar.
                    for x in range(image.width):
                        r, g, b, a = image.getpixel((x, y))
                        if a > 200 and r > 120 and b < 80:
                            border.append((x, y))
                            if r > 240 and g > 195:
                                face.append((x, y))
                spans.append([max(p[axis] for p in border) - min(p[axis] for p in border) -
                              (max(p[axis] for p in face) - min(p[axis] for p in face))
                              for axis in (0, 1)])
            for axis in (0, 1):
                self.assertGreaterEqual(spans[0][axis], 4 * scale)
                # Native downsampling can move each thresholded edge by
                # one pixel. HD coverage retains the exact half-width.
                self.assertAlmostEqual(spans[1][axis], spans[0][axis] / 2,
                                       delta=2 if scale == 1 else 1)

    def test_controls_and_footer_hints_have_navy_bars(self):
        for asset in ('common00/action_name.tm2', 'e_but_0.lzs', 'e_but_1.lzs'):
            with self.subTest(asset=asset):
                authored = {'schema': 1, 'locale': 'en', 'labels': [{'id': 'hint',
                    'asset': asset, 'text': 'OK', 'font_size': 12, 'rect': [0, 0, 64, 20]}]}
                render_labels(authored, self.images, require_title=False)
                bar_y = 2 if asset == 'e_but_1.lzs' else 16
                navy = (11, 19, 71, 255) if asset == 'e_but_1.lzs' else (0, 12, 100, 255)
                pixel = self.images[asset].getpixel((6, bar_y))
                self.assertLessEqual(max(abs(a - b) for a, b in zip(pixel, navy)), 1)
                if asset == 'e_but_1.lzs':
                    self.assertEqual(self.images[asset].getpixel((6, 16))[3], 0)
                if asset != 'e_but_1.lzs':
                    self.assertEqual(self.images[asset].getpixel((0, 0))[3], 0)

    def test_header_has_thick_white_border_and_hd_contains_finer_detail(self):
        from PIL import Image
        native = render_ui_label('Jungle', 24, 288, 64, header=True)
        hd = render_ui_label('Jungle', 24, 288, 64, header=True, output_scale=8)
        self.assertEqual(hd.size, (2304, 512))
        self.assertNotEqual(hd.tobytes(), native.resize(hd.size, Image.Resampling.NEAREST).tobytes())
        for image in (native, hd):
            white = [p for p in image.get_flattened_data() if p[3] > 200 and min(p[:3]) > 235]
            self.assertGreater(len(white), image.width // 2)
        plain = render_ui_label('Jungle', 24, 288, 64)
        self.assertFalse(any(p[3] > 200 and min(p[:3]) > 235 for p in plain.get_flattened_data()))

    @unittest.skipUnless(catalog('english-menu.json').is_file(), 'English catalog is not checked out')
    def test_footer_text_keeps_diagonal_button_pairs_and_clears_original_ink(self):
        from PIL import Image
        authored = json.loads(catalog('english-menu.json').read_text())
        authored['labels'] = [l for l in authored['labels'] if l['asset'] == 'e_but_1.lzs']
        self.assertEqual(len(authored['labels']), 9)
        # Observed Japanese text bounds and protected controller artwork. The
        # square at x=88 was previously erased by the Cancel text rectangle.
        ink = [(45, 83), (18, 84), (28, 80), (29, 104), (27, 93),
               (28, 80), (29, 67), (29, 67), (28, 68)]
        for scale in (1, 8):
            images = {'e_but_1.lzs': Image.new('RGBA', (128 * scale, 240 * scale))}
            regions = render_labels(authored, images, require_title=False, output_scale=scale)
            for label in authored['labels']:
                x, y, w, h = label['rect']
                row = y // 24
                first, last = ink[row]
                self.assertLessEqual(x, first)
                self.assertGreater(x + w, last)
                protected = (88, row * 24, 24, 24) if row in (0, 1) else (0, row * 24, 26, 24)
                bx, by, bw, bh = protected
                for rx, ry, rw, rh in regions['e_but_1.lzs']:
                    self.assertFalse(rx < bx + bw and bx < rx + rw and ry < by + bh and by < ry + rh,
                                     f'{label["id"]} erases a controller button')
                # Both bar orientations occur within this atlas, independently
                # of whether its controller buttons sit on the left or right.
                top = row in (0, 1, 6)
                sample_y = y + (1 if top else h - 4)
                pixel = images['e_but_1.lzs'].getpixel(((x + 4) * scale, sample_y * scale))
                navy = (2, 18, 103, 255)  # Lower half of the native two-tone bar.
                self.assertLessEqual(max(abs(a - b) for a, b in zip(pixel, navy)), 2)

    def test_cancel_text_keeps_native_navy_bar_opaque(self):
        from PIL import Image
        # Both native Cancel layouts: the text region cuts through a top
        # bar at y=4 and a footer bar at y=100 in the same atlas.
        labels = [{'id': f'cancel.{row}', 'asset': 'e_but_1.lzs', 'text': 'Cancel',
                   'font_size': 13, 'rect': [x, row * 24 + 4, width, 20]}
                  for row, x, width in ((1, 16, 72), (4, 26, 86))]
        for scale in (1, 4, 8):
            images = {'e_but_1.lzs': Image.new('RGBA', (128 * scale, 240 * scale))}
            render_labels({'schema': 1, 'locale': 'en', 'labels': labels}, images,
                          require_title=False, output_scale=scale)
            atlas = images['e_but_1.lzs']
            for y, left, right in ((28, 16, 88), (29, 16, 88), (32, 16, 88),
                                   (111, 26, 104), (118, 26, 104)):
                for py in range(y * scale, (y + 1) * scale):
                    for px in range(left * scale, right * scale):
                        self.assertEqual(atlas.getpixel((px, py))[3], 255,
                                         f'Cancel text leaves a hole in the navy bar at {(px, py, scale)}')
            # Uncovered portions retain the two native navy shades and join
            # the untouched atlas immediately outside the text rectangles.
            for x, y, color in ((20, 28, (11, 19, 71, 255)), (20, 32, (2, 18, 103, 255)),
                                 (28, 111, (11, 19, 71, 255)), (28, 118, (2, 18, 103, 255))):
                self.assertEqual(atlas.getpixel((x * scale, y * scale)), color)

    def test_rejects_missing_labels_overlap_overflow_and_uncovered_glyphs(self):
        variants = []
        missing = copy.deepcopy(self.catalog); missing['labels'].pop(1); variants.append(missing)
        duplicate = copy.deepcopy(self.catalog); duplicate['labels'].append(duplicate['labels'][0]); variants.append(duplicate)
        overlap = copy.deepcopy(self.catalog); overlap['labels'][2]['rect'] = [0, 0, 256, 24]; variants.append(overlap)
        overflow = copy.deepcopy(self.catalog); overflow['labels'][1]['text'] = 'A' * 100; variants.append(overflow)
        glyph = copy.deepcopy(self.catalog); glyph['labels'][1]['text'] = 'Café'; variants.append(glyph)
        bounds = copy.deepcopy(self.catalog); bounds['labels'][1]['rect'] = [-1, 0, 256, 24]; variants.append(bounds)
        text_bounds = copy.deepcopy(self.catalog); text_bounds['labels'][1]['text_rect'] = [250, 0, 32, 24]; variants.append(text_bounds)
        for variant in variants:
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                render_labels(variant, self.images)


if __name__ == '__main__':
    unittest.main()
