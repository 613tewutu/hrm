"""Exercise real terminal mouse events against hrm-tui (requires tmux)."""

import argparse
import re
import shlex
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMAND = re.compile(
    r"│[▶ ]*(\d+)\s+(inbox|outbox|add|sub|copyto|copyfrom|jumpifzero|jump)\b"
)


class Terminal:
    """Own an isolated tmux server and drive its terminal mouse protocol."""

    def __init__(self, binary: Path) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="hrm-drag-")
        self.base = ["tmux", "-S", str(Path(self.temporary.name) / "socket")]
        self.command(
            "new-session",
            "-d",
            "-x",
            "140",
            "-y",
            "52",
            "-s",
            "test",
            "-c",
            str(ROOT),
            f"{shlex.quote(str(binary))} --level 2",
        )
        self.wait_for("COMMANDS")

    def command(self, *arguments: str) -> str:
        """Run a command against this test's private server."""
        return subprocess.check_output(
            self.base + list(arguments), text=True, timeout=10
        )

    def close(self) -> None:
        """Stop only this test's server and remove its temporary directory."""
        subprocess.run(self.base + ["kill-server"], capture_output=True, timeout=10)
        self.temporary.cleanup()

    def screen(self) -> str:
        """Capture the visible terminal contents without escape codes."""
        return self.command("capture-pane", "-p", "-t", "test")

    def wait_for(self, text: str, timeout: float = 3) -> str:
        """Wait for a redraw containing the expected text."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            screen = self.screen()
            if text in screen:
                return screen
            time.sleep(0.03)
        raise AssertionError(f"Missing {text!r}:\n{self.screen()}")

    def keys(self, *keys: str) -> None:
        """Send named terminal keys and allow the UI to redraw."""
        self.command("send-keys", "-t", "test", *keys)
        time.sleep(0.08)

    def mouse(self, x: int, y: int, motion: str) -> None:
        """Send a press, left-button drag, or release at zero-based coordinates."""
        code = 32 if motion == "move" else 0
        end = "m" if motion == "release" else "M"
        self.command(
            "send-keys", "-t", "test", "-l", f"\x1b[<{code};{x + 1};{y + 1}{end}"
        )
        time.sleep(0.08)

    def locate(self, needle: str) -> tuple[int, int]:
        """Find the center of a visible label."""
        for y, line in enumerate(self.screen().splitlines()):
            if needle in line:
                return line.index(needle) + len(needle) // 2, y
        raise AssertionError(f"Missing label {needle!r}:\n{self.screen()}")

    def palette(self, name: str) -> tuple[int, int]:
        """Find a palette button independently of command-stack contents."""
        return self.locate(f"│ {name} │")

    def click_palette(self, name: str) -> None:
        """Use the click-to-insert fallback."""
        x, y = self.palette(name)
        self.mouse(x, y, "press")
        self.mouse(x, y, "release")

    def commands(self) -> list[str]:
        """Read visible instruction names in stack order."""
        return [match.group(2) for match in COMMAND.finditer(self.screen())]

    def command_position(self, number: int) -> tuple[int, int]:
        """Find a numbered command's text row."""
        for y, line in enumerate(self.screen().splitlines()):
            match = COMMAND.search(line)
            if match and int(match.group(1)) == number:
                return match.start() + 8, y
        raise AssertionError(f"Missing command {number}:\n{self.screen()}")

    def drag_palette(self, name: str, destination: tuple[int, int]) -> None:
        """Drag a palette command with a visible insertion line and floating card."""
        self.mouse(*self.palette(name), "press")
        self.mouse(*destination, "move")
        self.wait_for("INSERT " + name)
        self.mouse(*destination, "release")


class DragTests(unittest.TestCase):
    """Check insertion, cancellation, reordering, jump links, and scrolling."""

    binary: Path

    def setUp(self) -> None:
        """Start with an empty level-2 command stack."""
        self.terminal = Terminal(self.binary)
        self.addCleanup(self.terminal.close)

    def test_insert_reorder_cancel(self) -> None:
        """Drop at the start/middle/end and preserve the program on cancellation."""
        ui = self.terminal
        ui.drag_palette("inbox", ui.locate("Drop commands here"))
        self.assertEqual(ui.commands(), ["inbox"])
        self.assertNotIn("empty command", ui.screen())
        ui.drag_palette("outbox", ui.locate("Drop commands here"))
        x, y = ui.command_position(1)
        ui.drag_palette("add", (x, y - 1))
        self.assertEqual(ui.commands(), ["add", "inbox", "outbox"])
        x, y = ui.command_position(3)
        ui.drag_palette("sub", (x, y - 1))
        self.assertEqual(ui.commands(), ["add", "inbox", "sub", "outbox"])
        source = ui.command_position(1)
        destination = ui.locate("Drop commands here")
        ui.mouse(*source, "press")
        ui.mouse(*destination, "move")
        ui.wait_for("INSERT add")
        ui.mouse(*destination, "release")
        self.assertEqual(ui.commands(), ["inbox", "sub", "outbox", "add"])
        previous = ui.commands()
        ui.mouse(*ui.palette("copyto"), "press")
        ui.mouse(1, 5, "move")
        ui.mouse(1, 5, "release")
        self.assertEqual(ui.commands(), previous)
        ui.mouse(*ui.palette("copyfrom"), "press")
        ui.mouse(*ui.locate("Drop commands here"), "move")
        ui.keys("Escape")
        self.assertEqual(ui.commands(), previous)
        ui.mouse(1, 5, "release")
        ui.click_palette("outbox")
        self.assertEqual(ui.commands(), [*previous, "outbox"])

    def test_mouse_follow_preview(self) -> None:
        """Preserve the grabbed point and card size while following the pointer."""
        ui = self.terminal
        token = "│ copyto │"
        grab_x = len(token) // 2
        ui.mouse(*ui.palette("copyto"), "press")
        self.assertEqual(ui.screen().count(token), 1)
        ui.mouse(25, 20, "move")
        first = ui.screen().splitlines()
        self.assertEqual(first[20][25 - grab_x : 25 - grab_x + len(token)], token)
        ui.mouse(35, 25, "move")
        second = ui.screen().splitlines()
        self.assertEqual(second[25][35 - grab_x : 35 - grab_x + len(token)], token)
        self.assertEqual(ui.screen().count(token), 2)
        ui.mouse(139, 51, "move")
        self.assertEqual(ui.screen().splitlines()[50][140 - len(token) :], token)
        ui.mouse(139, 51, "release")
        self.assertEqual(ui.screen().count(token), 1)
        self.assertEqual(ui.commands(), [])

        # Grab away from the center: that exact point must stay under the mouse.
        px, py = ui.palette("copyto")
        ui.mouse(px - grab_x + 1, py, "press")
        ui.mouse(45, 22, "move")
        self.assertEqual(ui.screen().splitlines()[22][44 : 44 + len(token)], token)
        ui.keys("Escape")
        self.assertEqual(ui.screen().count(token), 1)
        self.assertEqual(ui.commands(), [])
        ui.mouse(1, 5, "release")

        # Existing program cards retain their wider frame, number, and operand.
        ui.click_palette("copyto")
        sx, sy = ui.command_position(1)
        original = ui.screen().splitlines()
        left = original[sy - 1].rfind("╭", 0, sx + 1)
        right = original[sy - 1].index("╮", left)
        width = right - left + 1
        label = original[sy][left : right + 1]
        ui.mouse(sx, sy, "press")
        ui.mouse(30, 20, "move")
        preview_left = 30 - (sx - left)
        preview = ui.screen().splitlines()
        self.assertEqual(preview[20][preview_left : preview_left + width], label)
        self.assertEqual(preview[19][preview_left], "╭")
        self.assertEqual(preview[19][preview_left + width - 1], "╮")
        ui.keys("Escape")
        self.assertEqual(ui.commands(), ["copyto"])

    def test_jump_links(self) -> None:
        """Keep landing cards attached while moving their source jump."""
        ui = self.terminal
        for command in ("inbox", "outbox", "jump"):
            ui.click_palette(command)
        ui.keys("t", "Up", "Up", "Enter")
        ui.wait_for("LANDING")
        source = ui.command_position(3)
        x, y = ui.command_position(1)
        ui.mouse(*source, "press")
        ui.mouse(x, y - 1, "move")
        ui.wait_for("INSERT jump")
        ui.mouse(x, y - 1, "release")
        self.assertEqual(ui.commands(), ["jump", "inbox", "outbox"])
        self.assertIn("LANDING", ui.screen())
        ui.keys("Delete")
        self.assertEqual(ui.commands(), ["inbox", "outbox"])
        self.assertNotIn("LANDING", ui.screen())
        ui.click_palette("jump")
        ui.keys("t", "Down", "Enter")
        ui.wait_for("LANDING")
        ui.drag_palette("copyto", ui.locate("LANDING"))
        self.assertEqual(ui.commands(), ["inbox", "outbox", "jump", "copyto"])
        self.assertIn("LANDING", ui.screen())

    def test_drag_scroll(self) -> None:
        """Scroll to the start when dragging above a long program's visible cards."""
        ui = self.terminal
        for _ in range(15):
            ui.click_palette("inbox")
        ui.mouse(*ui.palette("outbox"), "press")
        title_y = next(
            y for y, line in enumerate(ui.screen().splitlines()) if "COMMANDS" in line
        )
        ui.mouse(110, title_y + 7, "move")
        stack_left = ui.palette("inbox")[0]
        deadline = time.monotonic() + 4
        first_position = None
        while time.monotonic() < deadline:
            # The card under the pointer can obscure the instruction name.
            for y, line in enumerate(ui.screen().splitlines()):
                match = re.search(r"│[▶ ]*1\s", line)
                if match and match.start() > stack_left:
                    first_position = (match.start() + 4, y)
                    break
            if first_position:
                break
            time.sleep(0.05)
        self.assertIsNotNone(first_position, ui.screen())
        ui.mouse(*first_position, "move")
        ui.mouse(*first_position, "release")
        self.assertEqual(ui.commands()[0], "outbox")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    args, remaining = parser.parse_known_args()
    if not shutil.which("tmux"):
        parser.error("tmux is required for terminal drag tests")
    candidates = list((ROOT / "build").rglob("hrm-tui"))
    if args.binary is None and not candidates:
        parser.error("build hrm-tui first, or specify --binary")
    DragTests.binary = (
        args.binary or max(candidates, key=lambda path: path.stat().st_mtime)
    ).resolve()
    unittest.main(argv=[__file__, *remaining])
