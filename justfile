# Format C++/Python, wrapping long parameter lists without alignment padding.
fmt:
  python tools/format-code.py

# Verify the repository formatting policy without modifying files.
fmt-check:
  python tools/format-code.py --check

# Combine the core headers, implementations, and dedicated OJ main into one file.
pack-oj output="build/oj/main.cpp":
  python tools/pack-oj.py --output "{{output}}"

# Run C++11 kernel tests and compare linked and single-file OJ executables.
test:
  python tests/formatting.py
  python tests/run-tests.py

# Build and run the terminal workroom.
tui:
  xmake build hrm-tui
  xmake run hrm-tui

# Check YAML, command editing, playback history, and deterministic robot motion.
test-tui:
  xmake build hrm-app-tests
  xmake run hrm-app-tests

# Exercise dragging, scrolling, robot movement, and playback controls in tmux.
test-tui-drag:
  xmake build hrm-tui
  python tests/tui-drag.py

# Validate procedural audio without opening a device, and export previews for listening.
test-audio:
  xmake build hrm-audio-tests
  xmake run hrm-audio-tests build/audio-preview

# Run the graphical sound-room preview using the same audio module as the TUI.
gui:
  xmake build hrm-gui
  xmake run hrm-gui
