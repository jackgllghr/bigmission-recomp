#!/usr/bin/env python3
"""Build an English menu pack, optionally with structured gameplay label drafts.

Requires Pillow (pip install Pillow). Generated packs contain only replacement
pixels and source hashes; extracted Japanese textures stay in memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import os

from prepare_game import IsoImage
from translation_assets import archive_members, decode_lzs, decode_tim2
from translation_catalogs import catalog

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
SOURCE_PATH = 'PSP_GAME/USRDIR/Minigames/Menkolzs2.pac'
COMMON_SOURCE_PATH = 'PSP_GAME/USRDIR/data/common.lzs'
CONTROLS_SOURCE_PATH = 'PSP_GAME/USRDIR/data/common00.lzs'
ARCHIVES = {
    'menu': (SOURCE_PATH, False),
    'common': (COMMON_SOURCE_PATH, True),
    'common00': (CONTROLS_SOURCE_PATH, True),
    'tenso': ('PSP_GAME/USRDIR/data/tenso.lzs', True),
    'menko': ('PSP_GAME/USRDIR/Minigames/Menkolzs.pac', False),
    'lab': ('PSP_GAME/USRDIR/data/insidelabo.lzs', True),
}
# Logical dimensions and GE sampling extents, observed and verified in guest draws.
ASSETS = {
    'i_g_titl_l.lzs': ('title.logo', (352, 144), (512, 256, 352)),
    'b_but_b0.lzs': ('menu.labels', (256, 96), (256, 128, 256)),
    'e_nabi_a0.lzs': ('title.press_start', (256, 16), (256, 16, 256)),
    'e_but_0.lzs': ('menu.shortcuts', (128, 120), (128, 128, 128)),
    'e_but_1.lzs': ('dialog.buttons', (128, 240), (128, 256, 128)),
    'e_tit_0.lzs': ('screen.titles', (224, 480), (256, 512, 224)),
    'b_but_d0.lzs': ('dialog.yes_no', (96, 32), (128, 32, 96)),
    'b_but_d1.lzs': ('save.actions', (96, 48), (128, 64, 96)),
    'b_com_s0.lzs': ('save.labels', (96, 272), (128, 512, 96)),
    'b_ent_a0.lzs': ('name.tabs', (64, 96), (64, 128, 64)),
    'common00/action_name.tm2': ('gameplay.actions', (256, 512), (256, 512, 256)),
    'lab/o_kizai_0.tm2': ('laboratory.signs', (128, 64), (128, 64, 128)),
    'common/b_drm_0.tm2': ('pause.buttons', (128, 96), (128, 128, 128)),
    'common/e_tan_6.tm2': ('pause.counter', (128, 96), (128, 128, 128)),
    'common/e_stage.tm2': ('ui.common.e_stage', (160, 352), (256, 512, 160)),
    'tenso/e_s_title_1.tm2': ('ui.tenso.e_s_title_1', (288, 512), (512, 512, 288)),
    'tenso/e_s_title_2.tm2': ('ui.tenso.e_s_title_2', (288, 512), (512, 512, 288)),
    'tenso/e_s_title_3.tm2': ('ui.tenso.e_s_title_3', (288, 512), (512, 512, 288)),
    'common/e_end_0.tm2': ('ui.common.e_end_0', (256, 192), (256, 256, 256)),
    'common/e_end_2.tm2': ('ui.common.e_end_2', (256, 192), (256, 256, 256)),
    'common/e_boss.tm2': ('ui.common.e_boss', (192, 144), (256, 256, 192)),
    'common/e_kaza_c0.tm2': ('ui.common.e_kaza_c0', (128, 72), (128, 128, 128)),
    'common/e_nabi_a4.tm2': ('ui.common.e_nabi_a4', (288, 192), (512, 256, 288)),
    'common/e_nabi_a2.tm2': ('ui.common.e_nabi_a2', (320, 32), (512, 32, 320)),
    'common/e_nabi_b0.tm2': ('ui.common.e_nabi_b0', (160, 16), (256, 16, 160)),
    'common/e_nabi_b1.tm2': ('ui.common.e_nabi_b1', (96, 16), (128, 16, 96)),
    'common/e_nabi_b2.tm2': ('ui.common.e_nabi_b2', (256, 48), (256, 64, 256)),
    'common/e_kaza_0.tm2': ('ui.common.e_kaza_0', (96, 80), (128, 128, 96)),
    'menko/e_tit_1.lzs': ('ui.menko.e_tit_1', (224, 168), (256, 256, 224)),
    'menko/e_tit_2.lzs': ('ui.menko.e_tit_2', (224, 24), (256, 32, 224)),
    'menko/i_g_obj_0.lzs': ('ui.menko.i_g_obj_0', (64, 64), (64, 64, 64)),
    'menko/i_g_obj_1.lzs': ('ui.menko.i_g_obj_1', (64, 64), (64, 64, 64)),
    'menko/i_g_obj_2.lzs': ('ui.menko.i_g_obj_2', (64, 64), (64, 64, 64)),
    'menko/i_g_obj_3.lzs': ('ui.menko.i_g_obj_3', (64, 64), (64, 64, 64)),
    'menko/i_g_obj_4.lzs': ('ui.menko.i_g_obj_4', (64, 64), (64, 64, 64)),
    'menko/i_g_obj_5.lzs': ('ui.menko.i_g_obj_5', (64, 64), (64, 64, 64)),
    'menko/i_g_obj_6.lzs': ('ui.menko.i_g_obj_6', (64, 64), (64, 64, 64)),
    'common/b_but_c0.tm2': ('options.labels', (160, 64), (256, 64, 160)),
}


def digest(data):
    return hashlib.sha256(data).hexdigest().encode('ascii')


def name(text):
    data = text.encode('ascii')
    return struct.pack('<H', len(data)) + data


def encode_pixel_runs(rgba):
    """Bounded little-endian RGBA runs, independent of host byte order."""
    from itertools import groupby
    result = bytearray()
    for (pixel,), group in groupby(struct.iter_unpack('<I', rgba)):
        count = sum(1 for _ in group)
        while count:
            run = min(count, 65535)
            result += struct.pack('<HI', run, pixel)
            count -= run
    return result


def render_ui_label(text, size, width, height, alignment='center', *, controls=False,
                    header=False, top_hint_bar=False, output_scale=1):
    """Render UI styles in logical pixels, retaining detail at output_scale."""
    from PIL import Image, ImageDraw, ImageFont
    if alignment not in ('left', 'center'):
        raise ValueError('Label alignment must be left or center')
    scale = max(4, output_scale)
    font = ImageFont.load_default(size=size * scale)
    layer = Image.new('RGBA', (width * scale, height * scale))
    draw = ImageDraw.Draw(layer)
    body, outline, offset = scale / 2, 2 * scale, 3 * scale / 4
    body, outline, offset = int(body), int(outline), int(offset)
    fill = (255, 255, 2, 255)
    outside, inside = (135, 49, 35), (255, 236, 2)
    # Concentric one-texel strokes form a gradient across the outline, from
    # brown at the outside to yellow beside the solid letter face. Render at
    # the authoring resolution so HD layers retain the same logical width.
    layers = []
    for stroke in range(outline, body, -1):
        amount = (outline - stroke) / (outline - body - 1)
        color = tuple(round(a + (b - a) * amount) for a, b in zip(outside, inside)) + (255,)
        shift = 0 if controls or header else round(offset * (1 - amount))
        layers.append((shift, stroke, color))
    layers.append((0, body, fill))
    if header:
        layers.insert(0, (0, 4 * scale, (255, 255, 255, 255)))
    # Preserve the font's kerning, adding one supersampled pixel between glyphs.
    glyphs = [(char, font.getlength(text[:i + 1]) - font.getlength(char) + i * scale / 4)
              for i, char in enumerate(text)]
    boxes = [draw.textbbox((advance + shift, shift), char, font=font,
                           stroke_width=stroke)
             for char, advance in glyphs
             if char != ' '
             for shift, stroke, _ in layers]
    if not boxes:
        boxes = [draw.textbbox((0, 0), text, font=font, stroke_width=outline)]
    bounds = (min(b[0] for b in boxes), min(b[1] for b in boxes),
              max(b[2] for b in boxes), max(b[3] for b in boxes))
    tw, th = bounds[2] - bounds[0], bounds[3] - bounds[1]
    # Hinting at 8x can add a fraction of a logical pixel to a tight label.
    # Retain at least half a logical pixel of padding on the HD layer.
    margin = scale if output_scale == 1 else scale / 2
    if tw > layer.width - 2 * margin or th > layer.height - 2 * margin:
        raise ValueError('Text exceeds pixel budget; shorten it or reduce font_size')
    x = scale - bounds[0] if alignment == 'left' else (layer.width - tw) / 2 - bounds[0]
    y = (layer.height - th) / 2 - bounds[1]
    if controls:
        if top_hint_bar:
            # Dialog hints sample the middle of a longer top bar. Its rounded
            # ends and controller artwork live outside this replacement slot.
            draw.rectangle((0, 0, width * scale, 4 * scale - 1), fill=(11, 19, 71, 255))
        else:
            draw.rounded_rectangle((scale, (height - 8) * scale,
                                    (width - 1) * scale, (height - 1) * scale),
                                   radius=3.5 * scale, fill=(0, 12, 100, 255))
    for shift, stroke, color in layers:
        for char, advance in glyphs:
            draw.text((x + advance + shift, y + shift), char, font=font,
                      fill=color, stroke_width=stroke, stroke_fill=color)
    return layer.resize((width * output_scale, height * output_scale), Image.Resampling.LANCZOS)


def render_labels(catalog, images, require_title=True, *, output_scale=1):
    if catalog.get('schema') != 1 or catalog.get('locale') != 'en':
        raise ValueError('Unsupported catalog schema/locale')
    seen = set()
    rectangles = {}
    for label in catalog['labels']:
        key, text, asset = label['id'], label['text'], label['asset']
        if not isinstance(key, str) or not key or key in seen:
            raise ValueError('Missing/duplicate label ID')
        seen.add(key)
        if asset not in images or asset == 'i_g_titl_l.lzs':
            raise ValueError('Unknown label asset')
        if not isinstance(text, str) or not text or any(ord(c) < 32 or ord(c) > 126 for c in text):
            raise ValueError('English menu prototype supports printable ASCII labels only')
        rect = label['rect']
        if len(rect) != 4 or any(type(n) is not int for n in rect):
            raise ValueError('Invalid label rectangle')
        x, y, width, height = rect
        image = images[asset]
        if x < 0 or y < 0 or width <= 0 or height <= 0 or (x + width) * output_scale > image.width or (y + height) * output_scale > image.height:
            raise ValueError('Label rectangle outside image')
        for a, b, w, h in rectangles.setdefault(asset, []):
            if x < a + w and a < x + width and y < b + h and b < y + height:
                raise ValueError('Overlapping label rectangles')
        rectangles[asset].append(rect)
        size = label['font_size']
        if type(size) is not int or not 8 <= size <= 24:
            raise ValueError('Invalid font size')
        text_rect = label.get('text_rect', [0, 0, width, height])
        if len(text_rect) != 4 or any(type(n) is not int for n in text_rect):
            raise ValueError('Invalid text rectangle')
        tx, ty, tw, th = text_rect
        if tx < 0 or ty < 0 or tw <= 0 or th <= 0 or tx + tw > width or ty + th > height:
            raise ValueError('Text rectangle outside replacement region')
        try:
            layer = render_ui_label(text, size, tw, th, label.get('align', 'center'),
                                    controls=asset in ('common00/action_name.tm2', 'e_but_0.lzs', 'e_but_1.lzs'),
                                    header=(asset.startswith('tenso/e_s_title_') or asset in (
                                        'common/e_nabi_a4.tm2', 'common/e_nabi_a2.tm2')),
                                    top_hint_bar=asset == 'e_but_1.lzs' and y // 24 in (0, 1, 6),
                                    output_scale=output_scale)
        except ValueError as exc:
            raise ValueError(f'{key}: {exc}') from exc
        image.alpha_composite(layer, ((x + tx) * output_scale, (y + ty) * output_scale))
    expected = {'title.press_start', 'menu.new_game', 'menu.continue', 'menu.options', 'menu.gachi_king'}
    if require_title and not expected.issubset(seen):
        raise ValueError('The five title/main-menu labels are required')
    merged = {asset: merge_regions(regions) for asset, regions in rectangles.items()}
    if any(len(regions) > 64 for regions in merged.values()):
        raise ValueError('Too many image replacement regions')
    return merged


def merge_regions(regions):
    """Combine touching vertical slots without covering any unauthored pixels."""
    merged = []
    for x, y, width, height in sorted(regions, key=lambda r: (r[0], r[2], r[1])):
        if merged and merged[-1][0] == x and merged[-1][2] == width and merged[-1][1] + merged[-1][3] == y:
            merged[-1][3] += height
        else:
            merged.append([x, y, width, height])
    return merged


def render_line(run, canvas):
    from PIL import Image, ImageDraw, ImageFont
    if not 1 <= run['width'] <= 448 or not 1 <= run['glyph_count'] <= 64 or len(run['signature']) != 64:
        raise ValueError('Invalid help-line metadata')
    if any(c not in '0123456789abcdef' for c in run['signature']):
        raise ValueError('Invalid help-line signature')
    text = run['text']
    if not text or any(not 32 <= ord(c) <= 126 for c in text):
        raise ValueError('Help lines support printable ASCII')
    scale = 4
    layer = Image.new('RGBA', (run['width'] * scale, 16 * scale))
    font = ImageFont.load_default(size=12 * scale)
    draw = ImageDraw.Draw(layer)
    box = draw.textbbox((0, 0), text, font=font, stroke_width=scale)
    if box[2] - box[0] > layer.width - 2 * scale or box[3] - box[1] > layer.height - 2 * scale:
        raise ValueError('Help line exceeds pixel budget')
    draw.text((scale - box[0], (layer.height - box[3] + box[1]) / 2 - box[1]), text, font=font,
              fill=(255, 255, 255, 255), stroke_width=scale, stroke_fill=(0, 0, 0, 255))
    canvas.alpha_composite(layer.resize((run['width'], 16), Image.Resampling.LANCZOS))


def load_textures(iso):
    """Read only the known archives; identity remains the decoded texture hash."""
    sources, members = {}, {}
    for alias, (path, compressed) in ARCHIVES.items():
        data = iso.file(path, 6_000_000)
        sources[path] = data
        members[alias] = archive_members(decode_lzs(data) if compressed else data)
    textures = {}
    for asset in ASSETS:
        alias, member = asset.split('/') if '/' in asset else ('menu', asset)
        data = members[alias][member]
        textures[asset] = decode_tim2(decode_lzs(data) if member.endswith('.lzs') else data)
    font = decode_tim2(decode_lzs(members['menu']['c_font_0.lzs']))
    return sources, textures, font


def subtitle_records(directory):
    """The movies' subtitles: every <movie>.en.srt in `directory`, by movie name."""
    files = sorted(directory.glob('*.en.srt'))
    if not files:
        raise ValueError(f'No <movie>.en.srt subtitles in {directory}')
    if len(files) > 64:
        raise ValueError('Too many subtitled movies')
    result, total = struct.pack('<H', len(files)), 0
    for path in files:
        key = path.name[:-len('.en.srt')]
        if not 0 < len(key) <= 32 or any(not (c.islower() or c.isdigit() or c in '_-') or not c.isascii() for c in key):
            raise ValueError(f'Subtitle file name must be a lowercase movie name: {path.name}')
        data = path.read_bytes()
        data.decode('utf-8')
        if not data or len(data) > 256 * 1024 or b'\0' in data or b'-->' not in data:
            raise ValueError(f'Not a SubRip file of at most 256 KiB: {path.name}')
        total += len(data)
        result += name(key) + struct.pack('<I', len(data)) + data
    if total > 2 * 1024 * 1024:
        raise ValueError('Subtitles exceed 2 MiB')
    return result, len(files)


def build(iso_path, logo_path, catalog_path, output, gameplay_catalog=None, dialogue_catalog=None, subtitles=None):
    inputs = {iso_path.resolve(), logo_path.resolve(), catalog_path.resolve()}
    if gameplay_catalog is not None:
        inputs.add(gameplay_catalog.resolve())
    if dialogue_catalog is not None:
        inputs.add(dialogue_catalog.resolve())
        if output.with_suffix('.dialogue-layout.json').resolve() in inputs:
            raise ValueError('Dialogue layout report must not replace an input file')
    if output.resolve() in inputs:
        raise ValueError('Pack output must not replace an input file')
    from PIL import Image, ImageOps
    subtitle_bytes, subtitle_count = subtitle_records(subtitles) if subtitles is not None else (struct.pack('<H', 0), 0)
    supported = json.loads((PROFILE / 'config/supported_game.json').read_text(encoding='utf-8'))
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    with iso_path.open('rb') as stream:
        iso = IsoImage(stream)
        boot = iso.file(supported['executable_path'], 4_000_000)
        if hashlib.sha256(boot).hexdigest() != supported['executable_sha256']:
            raise ValueError('Unsupported executable; menu adapter supports UCJS10060 version 1.00')
        from dialogue_drafts import ui_caption_records
        compact_ui, ui_count = ui_caption_records(boot, catalog)
        compact_names, name_count = b'', 0
        if gameplay_catalog is not None:
            from dialogue_drafts import compact_name_records
            compact_names, name_count = compact_name_records(boot, json.loads(gameplay_catalog.read_text(encoding='utf-8')))
            from translation_drafts import merge_gameplay_catalog
            catalog = merge_gameplay_catalog(boot, catalog, json.loads(gameplay_catalog.read_text(encoding='utf-8')))
        dialogue_bytes, dialogue_count, dialogue_layout = b'', 0, []
        if dialogue_catalog is not None:
            from dialogue_drafts import dialogue_records
            dialogue_bytes, dialogue_layout, dialogue_count = dialogue_records(boot, json.loads(dialogue_catalog.read_text(encoding='utf-8')))
        if dialogue_count + name_count + ui_count > 512:
            raise ValueError('Too many compact message replacements')
        if name_count or ui_count:
            dialogue_bytes = (struct.pack('<H', dialogue_count + name_count + ui_count) +
                              (dialogue_bytes[2:] if dialogue_bytes else b'') +
                              compact_names[2:] + compact_ui[2:])
        sources, textures, font = load_textures(iso)
    images = {}
    for asset, (_, size, _) in ASSETS.items():
        texture = textures[asset]
        if (texture.width, texture.height) != size:
            raise ValueError('Unexpected source image dimensions: ' + asset)
        images[asset] = Image.new('RGBA', size)
    with Image.open(logo_path) as original:
        if original.width > 8192 or original.height > 8192:
            raise ValueError('Oversized logo')
        original.load()
        logo = ImageOps.contain(original.convert('RGBA'), ASSETS['i_g_titl_l.lzs'][1], Image.Resampling.LANCZOS)
    canvas = images['i_g_titl_l.lzs']
    canvas.alpha_composite(logo, ((canvas.width - logo.width) // 2, (canvas.height - logo.height) // 2))
    regions = render_labels(catalog, images)
    regions['i_g_titl_l.lzs'] = [(0, 0, 352, 144)]
    result = bytearray(b'SSTEXPK6')
    result += supported['disc_id'].encode('ascii') + supported['executable_sha256'].encode('ascii') + b'en'
    result += struct.pack('<I', len(sources))
    for source_path, source in sources.items():
        result += name(source_path) + digest(source)
    selected = {asset: spec for asset, spec in ASSETS.items() if asset in regions}
    result += struct.pack('<I', len(selected))
    for asset, (key, size, ge) in selected.items():
        rgba = images[asset].tobytes()
        result += name(key) + struct.pack('<5H', *size, *ge) + digest(textures[asset].rgba)
        result += struct.pack('<H', len(regions[asset]))
        for region in regions[asset]:
            result += struct.pack('<4H', *region)
        result += struct.pack('<I', len(rgba)) + rgba
    strings = catalog.get('strings', [])
    if len(strings) > 1024:
        raise ValueError('Too many dialog strings')
    result += struct.pack('<I', len(strings))
    occupied = []
    string_ids = set()
    import re
    token = re.compile(r'%(?:%|[-+ #0.0-9hlz]*[sdiuXxfc])')
    for entry in strings:
        key, offset, text = entry['id'], entry['offset'], entry['text']
        if key in string_ids or not isinstance(key, str) or not key:
            raise ValueError('Invalid/duplicate string ID')
        string_ids.add(key)
        if type(offset) is not int or not 0 <= offset < len(boot) or (offset and boot[offset - 1] != 0):
            raise ValueError('String offset must begin at a NUL-delimited slot')
        end = boot.find(b'\0', offset)
        if end < 0 or not 1 <= end - offset < 2048:
            raise ValueError('Invalid original string slot')
        source_string = boot[offset:end + 1]
        capacity = entry.get('capacity', len(source_string))
        if type(capacity) is not int or not len(source_string) <= capacity <= 2048 or offset + capacity > len(boot):
            raise ValueError(f'{key}: invalid string field capacity')
        if any(boot[end:offset + capacity]):
            raise ValueError(f'{key}: string field padding is not zero')
        source_field = boot[offset:offset + capacity]
        if not isinstance(text, str) or not text or any(ord(c) < 32 and c != '\n' or ord(c) > 126 for c in text):
            raise ValueError('Dialog translation supports ASCII and line breaks')
        lines = text.split('\n')
        if len(lines) > entry['max_lines'] or any(len(line) > entry['max_columns'] for line in lines):
            raise ValueError(f'{key}: dialog exceeds its layout budget')
        replacement = text.encode('ascii')
        if len(replacement) >= capacity:
            raise ValueError(f'{key}: translation exceeds its original byte slot')
        def placeholders(raw):
            matches = list(token.finditer(raw))
            if raw.count('%') != sum(len(m.group().replace('%', '')) == 0 and 2 or 1 for m in matches):
                raise ValueError('Unsupported printf placeholder')
            return [m.group() for m in matches if m.group() != '%%']
        if placeholders(source_string[:-1].decode('cp932')) != placeholders(replacement.decode('ascii')):
            raise ValueError(f'{key}: formatting tokens differ')
        if any(offset < b and a < offset + capacity for a, b in occupied):
            raise ValueError('Overlapping dialog string slots')
        occupied.append((offset, offset + capacity))
        result += name(key) + struct.pack('<II', offset, capacity) + digest(source_field)
        result += struct.pack('<H', len(replacement)) + replacement
    runs = catalog.get('glyph_runs', [])
    result += struct.pack('<H', len(runs))
    for run in runs:
        canvas = Image.new('RGBA', (512, 16))
        render_line(run, canvas)
        result += name(run['id']) + struct.pack('<HH', run['width'], 16) + digest(font.rgba)
        result += run['signature'].encode('ascii') + struct.pack('<H', run['glyph_count'])
        rgba = canvas.tobytes()
        result += struct.pack('<I', len(rgba)) + rgba
    result += dialogue_bytes or struct.pack('<H', 0)
    # Keep base-resolution pixels for native rendering and software consumers.
    # An optional high-resolution layer is rasterized from lettering, never
    # enlarged from that base bitmap. The supplied title logo stays on its path.
    hd_assets = [asset for asset in selected if asset != 'i_g_titl_l.lzs']
    result += struct.pack('<H', len(hd_assets))
    for asset in hd_assets:
        key, (width, height), _ = ASSETS[asset]
        hd = Image.new('RGBA', (width * 8, height * 8))
        labels = dict(catalog, labels=[label for label in catalog['labels'] if label['asset'] == asset])
        render_labels(labels, {asset: hd}, require_title=False, output_scale=8)
        pixel_runs = encode_pixel_runs(hd.tobytes())
        result += name(key) + struct.pack('<HI', 8, len(pixel_runs)) + pixel_runs
    result += subtitle_bytes
    # Atomic replacement of this explicit output file; no ISO writes or asset extraction.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.translation-', delete=False) as temporary:
        temporary.write(result)
        temporary.flush()
        os.fsync(temporary.fileno())
        staging = Path(temporary.name)
    try:
        staging.replace(output)
    finally:
        staging.unlink(missing_ok=True)
    if dialogue_catalog is not None:
        from extract_translation_labels import atomic_write
        atomic_write(output.with_suffix('.dialogue-layout.json'), json.dumps(dialogue_layout, indent=2) + '\n')
    print(f'Built {output}: {len(selected)} textures, {len(strings)} strings, {len(runs)} help lines, {dialogue_count} dialogue messages, {name_count} compact names, {ui_count} UI captions, {subtitle_count} subtitled movies; {len(result)} bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--logo', type=Path, default=ROOT / 'include/image/SaruSaruBigMission.png')
    parser.add_argument('--catalog', type=Path, default=catalog('english-menu.json'))
    parser.add_argument('--gameplay-catalog', type=Path, help='Include contextual English gameplay label drafts')
    parser.add_argument('--dialogue-catalog', type=Path, help='Include compact English dialogue drafts')
    parser.add_argument('--subtitles', type=Path, help="Include the movies' English subtitles: a folder of <movie>.en.srt")
    parser.add_argument('--output', type=Path, default=ROOT / 'out/translations/english-menu.sstp')
    args = parser.parse_args()
    try:
        build(args.iso, args.logo, args.catalog, args.output, args.gameplay_catalog, args.dialogue_catalog, args.subtitles)
    except (ValueError, UnicodeDecodeError, OSError, KeyError, ImportError, struct.error) as exc:
        parser.exit(1, f'build_menu_translation: {exc}\n')


if __name__ == '__main__':
    main()
