#!/usr/bin/env python3
"""Check a player APK contains the launcher and on-device builder, never game data."""
import argparse
from pathlib import PurePosixPath
import re
import zipfile

LIBRARIES = {'libSDL3.so', 'libavcodec.so', 'libavutil.so', 'libc++_shared.so', 'libmain.so',
             'libpsprecomp_runtime.so', 'libpsp_recomp.so', 'libsarusaru_llvm.so'}


def check(apk, allow_missing_compiler=False):
    with zipfile.ZipFile(apk) as archive:
        files = [n for n in archive.namelist() if not n.endswith('/')]
        libraries = {n for n in files if n.startswith('lib/')}
        expected = {'lib/arm64-v8a/' + n for n in LIBRARIES}
        without_compiler = expected - {'lib/arm64-v8a/libsarusaru_llvm.so'}
        if libraries != expected and not (allow_missing_compiler and libraries == without_compiler):
            raise ValueError('APK must contain exactly the arm64 launcher libraries and on-device compiler/code generator')
        for name in files:
            path = PurePosixPath(name)
            if path.is_absolute() or '..' in path.parts or path.name.startswith(('sarusaru_game', 'generated_')) or path.name.lower().endswith(('.elf', '.iso', '.sstp', '.bmstate', '.ppst')):
                raise ValueError('unexpected/game-derived APK file: ' + name)
            if name.startswith('assets/') and not (name == 'assets/sdk.zip' or re.fullmatch(
                    r'assets/resources/(fonts/[^/]+\.(ttf|txt)|[^/]+-(LICENSE|COPYING|SOURCE)[^/]*\.txt|title\.png)', name)):
                raise ValueError('unexpected asset: ' + name)
        if 'assets/sdk.zip' not in files:
            raise ValueError('missing on-device SDK')
        import io
        with zipfile.ZipFile(io.BytesIO(archive.read('assets/sdk.zip'))) as sdk:
            names = set(sdk.namelist())
            for required in ('sdk/include/game_abi.hpp', 'sdk/game_module_entry.cpp'):
                if required not in names:
                    raise ValueError('incomplete SDK: ' + required)
            if not allow_missing_compiler and not any(n.startswith('sdk/sysroot/usr/include/') for n in names):
                raise ValueError('missing compiler sysroot')
            for name in names:
                path = PurePosixPath(name)
                if path.is_absolute() or '..' in path.parts or 'generated' in path.parts or path.name.startswith(('sarusaru_game', 'generated_')) or path.suffix.lower() in ('.elf', '.iso', '.sstp', '.bmstate', '.ppst'):
                    raise ValueError('unexpected/game-derived SDK file: ' + name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk')
    parser.add_argument('--allow-missing-compiler', action='store_true', help='For debug CI artifacts only')
    args = parser.parse_args()
    check(args.apk, args.allow_missing_compiler)
