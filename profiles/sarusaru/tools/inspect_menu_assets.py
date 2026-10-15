#!/usr/bin/env python3
"""Inspect the title/menu/dialog assets from a user-owned ISO locally."""
import argparse
import hashlib
import json
from pathlib import Path

from prepare_game import IsoImage
from build_menu_translation import ASSETS, PROFILE, SOURCE_PATH, load_textures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, default=PROFILE / 'analysis/menu-assets')
    parser.add_argument('--previews', action='store_true', help='Write original PNG previews locally (requires Pillow; do not publish)')
    args = parser.parse_args()
    try:
        with args.iso.open('rb') as stream:
            iso = IsoImage(stream)
            sources, textures, _ = load_textures(iso)
        source = sources[SOURCE_PATH]
        report = {'source': SOURCE_PATH, 'sha256': hashlib.sha256(source).hexdigest(), 'assets': []}
        args.output.mkdir(parents=True, exist_ok=True)
        for asset, (key, _, ge) in ASSETS.items():
            texture = textures[asset]
            report['assets'].append({'id': key, 'member': asset,
                'logical_size': [texture.width, texture.height],
                'ge_extent_stride': list(ge), 'rgba_sha256': hashlib.sha256(texture.rgba).hexdigest()})
            if args.previews:
                from PIL import Image
                Image.frombytes('RGBA', (texture.width, texture.height), texture.rgba).save(args.output / (key + '.png'))
        (args.output / 'inventory.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Wrote local inventory to {args.output}; original previews must remain private')
    except (ValueError, OSError, KeyError, ImportError) as exc:
        parser.exit(1, f'inspect_menu_assets: {exc}\n')


if __name__ == '__main__':
    main()
