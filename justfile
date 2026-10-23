# Format C++ sources and headers using .clang-format.
fmt:
  rg --files --null src include tests -g '*.cpp' -g '*.hpp' | xargs -0 -r clang-format -i

# Combine the core headers, implementations, and dedicated OJ main into one file.
pack-oj output="build/oj/main.cpp":
  python tools/pack-oj.py --output "{{output}}"

# Run C++11 kernel tests and compare linked and single-file OJ executables.
test:
  python tests/run-tests.py

# Build and run the terminal workroom.
tui:
  xmake build hrm-tui
  xmake run hrm-tui

# Check the YAML configuration, command editor, jump links, and playback history.
test-tui:
  xmake build hrm-app-tests
  xmake run hrm-app-tests

# Exercise palette dragging, reordering, cancellation, and scrolling in tmux.
test-tui-drag:
  xmake build hrm-tui
  python tests/tui-drag.py
