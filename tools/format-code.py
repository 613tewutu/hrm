"""Run repository-native C++ and Python formatters without rewriting syntax."""

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def format_file(path: Path) -> str:
    """Format one authored source file using its native repository configuration."""
    command = (
        ["ruff", "format", "--stdin-filename", str(path), "-"]
        if path.suffix == ".py"
        else ["clang-format", "--assume-filename", str(path)]
    )
    return subprocess.run(
        command,
        input=path.read_text(encoding="utf-8"),
        text=True,
        capture_output=True,
        check=True,
        cwd=ROOT,
    ).stdout


def main() -> None:
    """Format authored C++/Python sources or report files needing changes."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    files = sorted(
        path
        for directory in ("src", "include", "tests", "tools")
        for path in (ROOT / directory).rglob("*")
        if path.suffix in {".cpp", ".hpp", ".py"}
    )
    changed: list[str] = []
    for path in files:
        formatted = format_file(path)
        if formatted != path.read_text(encoding="utf-8"):
            changed.append(str(path.relative_to(ROOT)))
            if not args.check:
                path.write_text(formatted, encoding="utf-8")
    if args.check and changed:
        parser.exit(1, "Formatting required:\n" + "\n".join(changed) + "\n")
    print(f"{'Checked' if args.check else 'Formatted'} {len(files)} source files")


if __name__ == "__main__":
    main()
