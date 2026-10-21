# Format C++ sources and headers using .clang-format.
fmt:
  rg --files --null src include -g '*.cpp' -g '*.hpp' | xargs -0 -r clang-format -i
