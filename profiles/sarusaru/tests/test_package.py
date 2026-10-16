"""The release packager takes only what players need, and never anything game-derived."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("package", SCRIPTS / "package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PlayerFilesTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.bin = Path(self.directory.name) / "build" / "bin"
        for name in ("sarusaru", "psp_recomp", "sarusaru_bootstrap", "sarusaru_settings_tests",
                     "lib/libpsprecomp_runtime.so", "resources/title.png", "sdk/include/game_abi.hpp",
                     "sdk/include/psprecomp/runtime.hpp", "sdk/game_module_entry.cpp"):
            path = self.bin / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"invented")

    def packaged(self):
        return sorted(relative.as_posix() for _, relative in package.player_files(self.bin, windows=False))

    def test_takes_only_the_player_files(self):
        self.assertEqual(self.packaged(), sorted([
            "sarusaru", "psp_recomp", "lib/libpsprecomp_runtime.so", "resources/title.png",
            "sdk/include/game_abi.hpp", "sdk/include/psprecomp/runtime.hpp", "sdk/game_module_entry.cpp"]))

    def test_refuses_a_game_module(self):
        (self.bin / "lib/sarusaru_game.so").write_bytes(b"invented")
        with self.assertRaisesRegex(ValueError, "game-derived"):
            self.packaged()

    def test_refuses_native_or_ppsspp_states(self):
        for name in ("quick.bmstate", "snapshot.ppst"):
            path = self.bin / "resources" / name
            path.write_bytes(b"invented")
            with self.assertRaisesRegex(ValueError, "game-derived"):
                self.packaged()
            path.unlink()

    def test_refuses_generated_code(self):
        (self.bin / "sdk/generated").mkdir()
        (self.bin / "sdk/generated/generated_unit_0000.cpp").write_bytes(b"invented")
        with self.assertRaisesRegex(ValueError, "game-derived"):
            self.packaged()

    def test_refuses_a_translation_pack(self):
        (self.bin / "resources/english-menu.sstp").write_bytes(b"invented")
        with self.assertRaisesRegex(ValueError, "translation"):
            self.packaged()

    def test_refuses_translation_catalogs(self):
        for name in ("resources/english-dialogue-v1.json", "resources/translations/glossary.json"):
            with self.subTest(name=name):
                path = self.bin / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"{}")
                with self.assertRaisesRegex(ValueError, "translation"):
                    self.packaged()
                path.unlink()

    def test_refuses_texture_dumps_and_packs(self):
        for name in ("resources/dump/textures.tsv", "resources/hd/pack.ini",
                     "resources/textures/0123456789abcdef0123456789abcdef.png",
                     "resources/0123456789abcdef0123456789abcdef.png", "resources/hd/wanted.txt"):
            with self.subTest(name=name):
                path = self.bin / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"invented")
                with self.assertRaisesRegex(ValueError, "texture"):
                    self.packaged()
                path.unlink()

    def test_refuses_a_binary_naming_the_build_directory(self):
        (self.bin / "sarusaru").write_bytes(b"rpath " + str(self.bin.parent).encode() + b"/bin/lib")
        with self.assertRaisesRegex(ValueError, "build directory"):
            self.packaged()

    def test_needs_the_kit(self):
        (self.bin / "sdk/include/game_abi.hpp").unlink()
        with self.assertRaisesRegex(ValueError, "sdk"):
            self.packaged()


@unittest.skipIf(sys.platform == "win32", "the Linux install layout is built on Linux")
class LinuxInstallTests(unittest.TestCase):
    def test_installs_the_launcher_and_its_desktop_files_under_the_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory) / "build"
            for name in ("sarusaru", "psp_recomp", "lib/libpsprecomp_runtime.so", "resources/title.png",
                         "sdk/include/game_abi.hpp", "sdk/game_module_entry.cpp"):
                path = build / "bin" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"invented")
            prefix = Path(directory) / "app"
            package.install_linux(build, prefix)
            self.assertEqual(Path(prefix / "bin/sarusaru").resolve(), (prefix / "lib/sarusaru/sarusaru").resolve())
            for name in ("lib/sarusaru/psp_recomp", "lib/sarusaru/lib/libpsprecomp_runtime.so",
                         "lib/sarusaru/sdk/include/game_abi.hpp", f"share/applications/{package.APP_ID}.desktop",
                         f"share/metainfo/{package.APP_ID}.metainfo.xml",
                         f"share/icons/hicolor/256x256/apps/{package.APP_ID}.png",
                         f"share/licenses/{package.APP_ID}/LICENSE"):
                self.assertTrue((prefix / name).is_file(), name)


if __name__ == "__main__":
    unittest.main()
