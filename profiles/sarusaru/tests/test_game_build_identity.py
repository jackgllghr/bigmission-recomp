"""Run the real CMake identity calculation without configuring graphics or audio."""
from pathlib import Path
import subprocess
import tempfile
import unittest


PROFILE = Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]


class GameBuildIdentity(unittest.TestCase):
    def identity(self, version):
        cmake = (PROFILE / 'CMakeLists.txt').read_text()
        calculation = cmake[cmake.index('file(GLOB _sarusaru_abi_inputs'):
                            cmake.index('configure_file(host/game_abi.hpp.in')]
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'identity.cmake'
            result = Path(directory) / 'identity.txt'
            script.write_text(f'''
set(PROJECT_SOURCE_DIR "{ROOT}")
set(CMAKE_CURRENT_SOURCE_DIR "{PROFILE}")
set(PROJECT_VERSION 0.1.0)
set(SARUSARU_RELEASE_VERSION "{version}")
{calculation.replace('CONFIGURE_DEPENDS', '')}
file(WRITE "{result}" "${{SARUSARU_GAME_ABI}}")
''')
            subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True, text=True)
            return result.read_text()

    def test_update_requires_a_new_installed_game(self):
        old = self.identity('0.2.0')
        self.assertEqual(old, self.identity('0.2.0'))
        self.assertNotEqual(old, self.identity('0.2.1'),
                            'Updating the app still finds the previous release’s installed game')


if __name__ == '__main__':
    unittest.main()
