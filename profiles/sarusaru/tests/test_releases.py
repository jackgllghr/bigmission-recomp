"""Release identity, exact platform feeds, and game-free installable APKs."""
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import release_metadata
import check_android_apk


class Releases(unittest.TestCase):
    def test_identity_and_invalid_tags(self):
        self.assertEqual(release_metadata.version('v1.2.3'), '1.2.3')
        self.assertEqual(release_metadata.android_version_code('v0.2.0'), 2001)
        self.assertLess(release_metadata.android_version_code('0.1.999'), release_metadata.android_version_code('0.2.0'))
        for invalid in ('0.1', '01.2.3', '1.2.3-beta', '1.1000.0', '../1.2.3', '999999.0.0'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                release_metadata.version(invalid)

    def test_feed_requires_every_platform_and_exact_version(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            with self.assertRaises(ValueError):
                release_metadata.update_feed('v0.2.0', path)
            for host, ext in [('windows-x64', '.zip'), ('macos-arm64', '.zip'),
                              ('linux-x64', '.AppImage'), ('linux-arm64', '.AppImage')]:
                (path / f'SaruSaruRecomp-v0.2.0-{host}{ext}').write_bytes(b'package')
            feed = release_metadata.update_feed('v0.2.0', path)
            self.assertIn('version\t0.2.0\n', feed)
            self.assertEqual(len(feed.splitlines()), 6)
            self.assertNotIn('android', feed)
            with self.assertRaises(ValueError):
                release_metadata.update_feed('v0.3.0', path)

    def apk(self, path, extra=None, compiler=True):
        sdk_bytes = io.BytesIO()
        with zipfile.ZipFile(sdk_bytes, 'w') as sdk:
            sdk.writestr('sdk/include/game_abi.hpp', b'header')
            sdk.writestr('sdk/game_module_entry.cpp', b'own entry')
            sdk.writestr('sdk/sysroot/usr/include/stdio.h', b'header')
        with zipfile.ZipFile(path, 'w') as apk:
            for name in check_android_apk.LIBRARIES:
                if compiler or name != 'libsarusaru_llvm.so':
                    apk.writestr('lib/arm64-v8a/' + name, b'library')
            apk.writestr('assets/sdk.zip', sdk_bytes.getvalue())
            apk.writestr('assets/resources/title.png', b'logo')
            if extra:
                apk.writestr(extra, b'unexpected')

    def test_installable_apk_and_game_data_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'release.apk'
            self.apk(path)
            check_android_apk.check(path)
            check_android_apk.check(path, allow_missing_compiler=True)
            for extra in ('lib/arm64-v8a/sarusaru_game.so', 'assets/BOOT.elf',
                          'assets/translation.sstp', 'assets/../unsafe', 'assets/random.png'):
                self.apk(path, extra)
                with self.subTest(extra=extra), self.assertRaises(ValueError):
                    check_android_apk.check(path)
            self.apk(path, compiler=False)
            with self.assertRaises(ValueError):
                check_android_apk.check(path)
            check_android_apk.check(path, allow_missing_compiler=True)


if __name__ == '__main__':
    unittest.main()
