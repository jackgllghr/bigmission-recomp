#!/usr/bin/env python3
"""Validate stable release identity and create the small desktop update feed."""
import argparse
from pathlib import Path
import re

REPOSITORY = 'https://github.com/jackgllghr/sarusaru-recomp'
HEADER = 'SaruSaruRecomp-Update-v1'


def version(text):
    match = re.fullmatch(r'v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', text)
    if not match:
        raise ValueError('release version must be X.Y.Z or vX.Y.Z (stable releases only)')
    numbers = tuple(map(int, match.groups()))
    if numbers[1] > 999 or numbers[2] > 999 or numbers[0] * 1_000_000 + numbers[1] * 1000 + numbers[2] + 1 > 2_100_000_000:
        raise ValueError('version cannot be represented by Android versionCode')
    return '.'.join(map(str, numbers))


def android_version_code(text):
    major, minor, patch = map(int, version(text).split('.'))
    return major * 1_000_000 + minor * 1000 + patch + 1


def update_feed(release, directory):
    release = version(release)
    lines = [HEADER, 'version\t' + release]
    for platform, extension in [('windows-x64', '.zip'), ('macos-arm64', '.zip'),
                                ('linux-x64', '.AppImage'), ('linux-arm64', '.AppImage')]:
        name = f'SaruSaruRecomp-v{release}-{platform}{extension}'
        asset = directory / name
        if not asset.is_file():
            raise ValueError(f'missing release asset: {name}')
        lines.append(f'{platform}\t{REPOSITORY}/releases/download/v{release}/{name}')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version')
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--android-code', action='store_true')
    args = parser.parse_args()
    release = version(args.version)
    if args.assets:
        (args.assets / 'update-info.txt').write_text(update_feed(release, args.assets), encoding='ascii')
    print(android_version_code(release) if args.android_code else release)


if __name__ == '__main__':
    main()
