"""Exercise the developer launcher's real main path without building or opening a window."""

import configparser
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("build_and_run_macos", SCRIPTS / "build_and_run_macos.py")
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class LauncherSettingsTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.profile = Path(self.directory.name)
        (self.profile / "resources").mkdir()
        self.resource_settings = self.profile / "resources/settings.ini"
        self.resource_settings.write_text("[graphics]\nexperimental_game_frame_rate = 60\n")
        self.user_settings = self.profile / "user-settings.ini"
        self.user_settings.write_text("[graphics]\nexperimental_game_frame_rate = 0\n")
        self.iso = self.profile / "game.iso"
        self.iso.touch()

    def command(self, game_args=(), environment=None):
        with patch.object(launcher, "PROFILE", self.profile), \
             patch.object(launcher, "BUILD", self.profile / "build"), \
             patch.object(launcher, "build") as build, \
             patch.object(launcher.subprocess, "call", return_value=0) as launch, \
             patch.object(sys, "argv", ["build_and_run_macos.py", "--iso", str(self.iso), "--", *game_args]), \
             patch.dict(os.environ, {"SARUSARU_SETTINGS": environment or ""}):
            self.assertEqual(launcher.main(), 0)
            build.assert_called_once_with(self.iso.resolve(), 2)
            return launch.call_args.args[0]

    def test_default_launch_applies_repository_60fps_setting(self):
        command = self.command()
        # This reproduces the reported symptom: the editable repo file says 60,
        # but the old launcher chooses a separate per-user file at native cadence.
        selected = Path(command[command.index("--settings") + 1]) if "--settings" in command else self.user_settings
        settings = configparser.ConfigParser()
        settings.read(selected)
        self.assertEqual(settings.getint("graphics", "experimental_game_frame_rate"), 60,
                         "launcher ignored the edited repository settings and kept native game cadence")
        self.assertEqual(selected, self.resource_settings.resolve())

    def test_explicit_settings_take_precedence(self):
        command = self.command(["--settings", str(self.user_settings), "--scale", "3"])
        self.assertEqual(command.count("--settings"), 1)
        self.assertEqual(command[command.index("--settings") + 1], str(self.user_settings))
        self.assertIn("--scale", command)

    def test_environment_settings_take_precedence(self):
        command = self.command(environment=str(self.user_settings))
        self.assertNotIn("--settings", command)

    def test_explicit_settings_override_environment(self):
        command = self.command(["--settings", str(self.resource_settings)], environment=str(self.user_settings))
        self.assertEqual(command.count("--settings"), 1)
        self.assertEqual(command[command.index("--settings") + 1], str(self.resource_settings))


if __name__ == "__main__":
    unittest.main()
