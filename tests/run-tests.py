"""Build C++11 regression tests and compare linked and exported OJ programs."""

import os
import runpy
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/tests"
COMPILER = shlex.split(os.environ.get("CXX", "c++"))
FLAGS = ["-std=c++11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]
AMALGAMATE = runpy.run_path(str(ROOT / "tools/pack-oj.py"))["amalgamate"]


def run(
    command: list[str], input_text: str | None = None
) -> subprocess.CompletedProcess[str]:
    """Execute a command with captured output and a bounded runtime."""
    return subprocess.run(
        command, input=input_text, text=True, capture_output=True, timeout=120, cwd=ROOT
    )


class KernelTests(unittest.TestCase):
    """Check kernel semantics, standalone headers, and OJ submission parity."""

    @classmethod
    def setUpClass(cls) -> None:
        """Compile the regression executable and both forms of the OJ program."""
        BUILD.mkdir(parents=True, exist_ok=True)
        sources = [str(path) for path in sorted((ROOT / "src/core").glob("*.cpp"))]
        for name, main in (
            ("core-tests", "tests/core.cpp"),
            ("hrm-oj", "src/oj/main.cpp"),
        ):
            result = run(
                COMPILER
                + FLAGS
                + ["-Iinclude", *sources, main, "-o", str(BUILD / name)]
            )
            if result.returncode:
                raise RuntimeError(result.stderr)
        packed = BUILD / "submission.cpp"
        result = run([sys.executable, "tools/pack-oj.py", "--output", str(packed)])
        if result.returncode:
            raise RuntimeError(result.stderr)
        # Compile outside the repository, without any project include path.
        result = subprocess.run(
            COMPILER + FLAGS + [str(packed), "-o", str(BUILD / "hrm-oj-single")],
            capture_output=True,
            text=True,
            timeout=120,
            cwd=BUILD,
        )
        if result.returncode:
            raise RuntimeError(result.stderr)

    def test_kernel(self) -> None:
        """Run the state-transition and semantic regressions."""
        result = run([str(BUILD / "core-tests")])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_headers(self) -> None:
        """Ensure each public header compiles independently under C++11."""
        for header in sorted((ROOT / "include/hrm").glob("*.hpp")):
            with self.subTest(header=header.name):
                result = run(
                    COMPILER + FLAGS + ["-Iinclude", "-x", "c++", "-fsyntax-only", "-"],
                    f'#include "hrm/{header.name}"\n',
                )
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_oj(self) -> None:
        """Verify exact stdout and identical linked/single-file behavior."""
        cases = [
            (1, ["inbox", "outbox", "inbox", "outbox"], "Success"),
            (1, ["inbox", "outbox"], "Fail"),
            (1, [], "Fail"),
            (1, ["outbox"], "Error on instruction 1"),
            (1, ["jump 1"], "Error on instruction 1"),
            (
                1,
                ["inbox", "outbox", "inbox", "outbox", "outbox"],
                "Error on instruction 5",
            ),
            (1, ["inbox", "outbox", "inbox", "outbox", "inbox", "bad"], "Success"),
            (2, ["jump 3", "", "inbox"], "Fail"),
            (2, ["jump 2", ""], "Error on instruction 2"),
            (2, ["inbox", "jumpifzero 99"], "Error on instruction 2"),
            (2, ["copyfrom -1"], "Error on instruction 1"),
            (2, ["copyfrom 3"], "Error on instruction 1"),
            (2, ["copyfrom 0"], "Error on instruction 1"),
            (2, ["inbox 1"], "Error on instruction 1"),
            (2, ["jump 1x"], "Error on instruction 1"),
            (2, ["jump 9223372036854775808"], "Error on instruction 1"),
            (2, ["jump 9223372036854775807"], "Error on instruction 1"),
            (
                2,
                [
                    "inbox",
                    "copyto 0",
                    "inbox",
                    "copyto 1",
                    "copyfrom 0",
                    "sub 1",
                    "outbox",
                    "copyfrom 1",
                    "sub 0",
                    "outbox",
                    "jump 1",
                ],
                "Success",
            ),
            (
                3,
                [
                    "inbox",
                    "copyto 0",
                    "inbox",
                    "sub 0",
                    "jumpifzero 7",
                    "jump 1",
                    "copyfrom 0",
                    "outbox",
                    "jump 1",
                ],
                "Success",
            ),
        ]
        for level, lines, expected in cases:
            payload = f"{level}\n{len(lines)}\n" + "".join(
                line + "\n" for line in lines
            )
            for newline in ("\n", "\r\n"):
                for binary in ("hrm-oj", "hrm-oj-single"):
                    with self.subTest(
                        level=level, lines=lines, binary=binary, newline=repr(newline)
                    ):
                        result = run(
                            [str(BUILD / binary)], payload.replace("\n", newline)
                        )
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(result.stdout, expected + "\n")
                        self.assertEqual(result.stderr, "")

    def test_bad_envelope(self) -> None:
        """Keep envelope errors separate from instruction verdicts."""
        for payload in (
            "",
            "4\n0\n",
            "1\n-1\n",
            "1\n2\ninbox\n",
            "1\n1x\n",
            "1\n0 extra\n",
        ):
            for binary in ("hrm-oj", "hrm-oj-single"):
                with self.subTest(payload=payload, binary=binary):
                    result = run([str(BUILD / binary)], payload)
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "Invalid OJ input\n")

    def test_packaging(self) -> None:
        """Ensure packing is deterministic and removes all project includes."""
        generated = (BUILD / "submission.cpp").read_text(encoding="utf-8")
        self.assertEqual(generated, AMALGAMATE(ROOT))
        self.assertEqual(generated, AMALGAMATE(ROOT))
        self.assertNotIn('#include "hrm/', generated)
        self.assertNotIn("#pragma once", generated)
        self.assertEqual(generated.count("int main()"), 1)

    def test_include_errors(self) -> None:
        """Reject cyclic includes and includes outside the public kernel tree."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            headers = root / "include/hrm"
            headers.mkdir(parents=True)
            (headers / "core.hpp").write_text(
                '#include "hrm/loop.hpp"\n', encoding="utf-8"
            )
            (headers / "loop.hpp").write_text(
                '#include "hrm/core.hpp"\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "Include cycle"):
                AMALGAMATE(root)
            (headers / "core.hpp").write_text(
                '#include "../outside.hpp"\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "Unsupported local include"):
                AMALGAMATE(root)


if __name__ == "__main__":
    unittest.main()
