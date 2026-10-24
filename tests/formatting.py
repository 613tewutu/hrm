"""Check concise short calls and indentation-only wrapping of long lists."""

import ast
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def format_source(source: str, language: str) -> str:
    """Format a fixture with the repository's native language configuration."""
    command = (
        ["ruff", "format", "--stdin-filename", str(ROOT / "tools/example.py"), "-"]
        if language == "python"
        else ["clang-format", "--assume-filename", str(ROOT / "src/core/example.cpp")]
    )
    return subprocess.run(command, input=source, text=True, capture_output=True, check=True).stdout


class FormattingTests(unittest.TestCase):
    """Keep short argument lists inline and wrap long lists without column padding."""

    def test_cpp_layout(self) -> None:
        """Preserve short signatures, literals, and calls while wrapping long lists."""
        source = """// fake(a, b) must remain comment text.
const char* text = R"tag(fake(1, 2))tag";
void sample(int first, int second);
void single(int value) { sample(value, nested(value)); }
void many(int first_parameter_with_a_very_long_descriptive_name, int second_parameter_with_a_very_long_descriptive_name);
"""
        result = format_source(source, "cpp")
        self.assertIn("void single(int value)", result)
        self.assertIn("sample(value, nested(value));", result)
        self.assertIn("void sample(int first, int second);", result)
        self.assertIn(
            "void many(\n  int first_parameter_with_a_very_long_descriptive_name,\n"
            "  int second_parameter_with_a_very_long_descriptive_name\n);",
            result,
        )
        self.assertIn("// fake(a, b) must remain comment text.", result)
        self.assertIn('R"tag(fake(1, 2))tag"', result)
        self.assertEqual(result, format_source(result, "cpp"))

    def test_python_layout(self) -> None:
        """Collapse redundant commas/newlines and preserve syntax during normal wrapping."""
        source = """def single(
    value,
):
    destination = ui.locate(
        "Drop commands here",
    )
    return any(item for item in [value])

result = calculate(first_parameter_with_a_very_long_name, second_parameter_with_a_very_long_name, third_parameter_with_a_very_long_name)
"""
        result = format_source(source, "python")
        self.assertIn("def single(value):", result)
        self.assertIn('destination = ui.locate("Drop commands here")', result)
        self.assertIn(
            "result = calculate(\n    first_parameter_with_a_very_long_name,\n"
            "    second_parameter_with_a_very_long_name,\n"
            "    third_parameter_with_a_very_long_name,\n)",
            result,
        )
        self.assertEqual(ast.dump(ast.parse(source)), ast.dump(ast.parse(result)))
        self.assertEqual(result, format_source(result, "python"))


if __name__ == "__main__":
    unittest.main()
