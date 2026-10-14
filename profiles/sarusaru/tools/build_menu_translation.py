#!/usr/bin/env python3
"""Build an English main-menu texture pack from your ISO and the supplied logo.

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

PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
SOURCE_PATH = 'PSP_GAME/USRDIR/Minigames/Menkolzs2.pac'
# Logical dimensions and GE sampling extents, observed and verified in guest draws.
ASSETS = {
    'i_g_titl_l.lzs': ('title.logo', (352, 144), (512, 256, 352)),
    'b_but_b0.lzs': ('menu.labels', (256, 96), (256, 128, 256)),
    'e_nabi_a0.lzs': ('title.press_start', (256, 16), (256, 16, 256)),
}


def digest(data):
    return hashlib.sha256(data).hexdigest().encode('ascii')


def name(text):
    data = text.encode('ascii')
    return struct.pack('<H', len(data)) + data


def render_labels(catalog, images):
    from PIL import Image, ImageDraw, ImageFont
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
        if x < 0 or y < 0 or width <= 0 or height <= 0 or x + width > image.width or y + height > image.height:
            raise ValueError('Label rectangle outside image')
        for a, b, w, h in rectangles.setdefault(asset, []):
            if x < a + w and a < x + width and y < b + h and b < y + height:
                raise ValueError('Overlapping label rectangles')
        rectangles[asset].append(rect)
        size = label['font_size']
        if type(size) is not int or not 8 <= size <= 24:
            raise ValueError('Invalid font size')
        scale = 4
        # Pillow's bundled default font: no platform font dependency or font file in the pack.
        font = ImageFont.load_default(size=size * scale)
        layer = Image.new('RGBA', (width * scale, height * scale))
        draw = ImageDraw.Draw(layer)
        stroke = scale
        bounds = draw.textbbox((0, 0), text, font=font, stroke_width=stroke)
        tw, th = bounds[2] - bounds[0], bounds[3] - bounds[1]
        if tw > layer.width - 2 * scale or th > layer.height - 2 * scale:
            raise ValueError(f'{key}: text exceeds pixel budget; shorten it or reduce font_size')
        draw.text(((layer.width - tw) / 2 - bounds[0], (layer.height - th) / 2 - bounds[1]),
                  text, font=font, fill=(255, 224, 32, 255), stroke_width=stroke,
                  stroke_fill=(220, 55, 8, 255))
        image.alpha_composite(layer.resize((width, height), Image.Resampling.LANCZOS), (x, y))
    expected = {'title.press_start', 'menu.new_game', 'menu.continue', 'menu.options', 'menu.gachi_king'}
    if seen != expected:
        raise ValueError('This atlas prototype requires all five menu labels; incomplete catalogs are rejected')


def build(iso_path, logo_path, catalog_path, output):
    if output.resolve() in {iso_path.resolve(), logo_path.resolve(), catalog_path.resolve()}:
        raise ValueError('Pack output must not replace an input file')
    from PIL import Image, ImageOps
    supported = json.loads((PROFILE / 'config/supported_game.json').read_text())
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    with iso_path.open('rb') as stream:
        iso = IsoImage(stream)
        boot = iso.file(supported['executable_path'], 4_000_000)
        if hashlib.sha256(boot).hexdigest() != supported['executable_sha256']:
            raise ValueError('Unsupported executable; menu adapter supports UCJS10060 version 1.00')
        source = iso.file(SOURCE_PATH, 6_000_000)
        members = archive_members(source)
        textures = {asset: decode_tim2(decode_lzs(members[asset])) for asset in ASSETS}
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
    render_labels(catalog, images)
    result = bytearray(b'SSTEXPK2')
    result += supported['disc_id'].encode('ascii') + supported['executable_sha256'].encode('ascii') + b'en'
    result += struct.pack('<I', 1) + name(SOURCE_PATH) + digest(source)
    result += struct.pack('<I', len(ASSETS))
    for asset, (key, size, ge) in ASSETS.items():
        rgba = images[asset].tobytes()
        result += name(key) + struct.pack('<5H', *size, *ge) + digest(textures[asset].rgba)
        result += struct.pack('<I', len(rgba)) + rgba
    strings = catalog.get('strings', [])
    if len(strings) > 256:
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
        if not isinstance(text, str) or not text or any(ord(c) < 32 and c != '\n' or ord(c) > 126 for c in text):
            raise ValueError('Dialog translation supports ASCII and line breaks')
        lines = text.split('\n')
        if len(lines) > entry['max_lines'] or any(len(line) > entry['max_columns'] for line in lines):
            raise ValueError(f'{key}: dialog exceeds its layout budget')
        replacement = text.encode('ascii')
        if len(replacement) >= len(source_string):
            raise ValueError(f'{key}: translation exceeds its original byte slot')
        def placeholders(raw):
            matches = list(token.finditer(raw))
            if raw.count('%') != sum(len(m.group().replace('%', '')) == 0 and 2 or 1 for m in matches):
                raise ValueError('Unsupported printf placeholder')
            return [m.group() for m in matches if m.group() != '%%']
        if placeholders(source_string[:-1].decode('cp932')) != placeholders(replacement.decode('ascii')):
            raise ValueError(f'{key}: formatting tokens differ')
        if any(offset < b and a < end + 1 for a, b in occupied):
            raise ValueError('Overlapping dialog string slots')
        occupied.append((offset, end + 1))
        result += name(key) + struct.pack('<II', offset, len(source_string)) + digest(source_string)
        result += struct.pack('<H', len(replacement)) + replacement
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
    print(f'Built {output}: {len(ASSETS)} textures, {len(result)} bytes; Japanese fallback elsewhere')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--logo', type=Path, default=ROOT / 'include/image/SaruSaruBigMission.png')
    parser.add_argument('--catalog', type=Path, default=PROFILE / 'resources/translations/english-menu.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/translations/english-menu.sstp')
    args = parser.parse_args()
    try:
        build(args.iso, args.logo, args.catalog, args.output)
    except (ValueError, OSError, KeyError, ImportError, struct.error) as exc:
        parser.exit(1, f'build_menu_translation: {exc}\n')


if __name__ == '__main__':
    main()
