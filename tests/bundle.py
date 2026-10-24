"""Verify the copied TUI runs all built-in levels without any external game resources."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TITLES = (
    "First day",
    "Two sides",
    "Equal partners",
    "Triple it",
    "Same sign",
    "Absolute delivery",
    "Larger partner",
    "Running total",
    "Batch totals",
    "Product line",
)


class BundleTests(unittest.TestCase):
    """Use only a copied executable, isolated configuration, and temporary working directory."""

    def setUp(self) -> None:
        """Copy the explicit bundle artifact without its sibling build files."""
        self.temporary = tempfile.TemporaryDirectory(prefix="hrm-bundle-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.binary = self.directory / "hrm-tui"
        shutil.copy2(ROOT / "build/bundle/hrm-tui", self.binary)
        self.environment = dict(os.environ, XDG_CONFIG_HOME=str(self.directory / "config"))

    def run_preview(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        """Run a resource-free preview without accessing any audio device."""
        return subprocess.run(
            [str(self.binary), "--preview", "--width", "140", "--height", "52", *arguments],
            cwd=self.directory,
            env=self.environment,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_all_levels(self) -> None:
        """Load each of the ten levels with no assets directory beside the binary or cwd."""
        for number, title in enumerate(TITLES, 1):
            with self.subTest(level=number):
                result = self.run_preview("--level", str(number))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(title, result.stdout)
        self.assertFalse((self.directory / "assets").exists())

    def test_explicit_override(self) -> None:
        """Ignore stray YAML by default while still allowing explicit custom configuration."""
        assets = self.directory / "assets"
        assets.mkdir()
        configuration = assets / "game.yaml"
        configuration.write_text("broken: [", encoding="utf-8")
        self.assertEqual(self.run_preview("--level", "10").returncode, 0)
        self.assertNotEqual(self.run_preview("--config", str(configuration)).returncode, 0)
        configuration.write_text(
            "version: 1\ntitle: Custom\nlevels:\n - id: 42\n   title: Custom delivery\n"
            "   description: Send the block.\n   input: [9]\n   target: [9]\n"
            "   floor_size: 0\n   instructions: [inbox, outbox]\n",
            encoding="utf-8",
        )
        result = self.run_preview("--config", str(configuration), "--level", "42")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Custom delivery", result.stdout)


if __name__ == "__main__":
    unittest.main()
