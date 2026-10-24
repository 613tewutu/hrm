# Repository conventions

- Keep `include/hrm/`, `src/core/`, and `src/oj/` compatible with C++11.
  Application adapters and frontends may use C++20.
- Use C++ `///` documentation comments following Chitin's Rust convention.
  Simple accessors and setters need one sentence. Document complex behavior
  with a summary, relevant `@param` entries, and `@return` for meaningful return
  values. Explain failure, ownership, and state changes when they affect callers.
  Do not repeat public-header documentation on out-of-line definitions or add
  empty parameter/return sections to functions that do not need them.
- Keep short function signatures and calls inline, especially single-parameter
  lists. Only wrap lists when their length or complexity requires it. For a
  wrapped list, start on the line after `(` with one parameter/argument per line
  and ordinary block indentation, never padding under the function name or
  opening parenthesis. Use the 100-column formatter limit as the default guide.
- Use spaces only for syntactic separation and indentation. Do not pad
  assignments, declarations, continuations, operators, comments, or macro
  backslashes to align columns. Explicit definition tables/lists may use visual
  column alignment where it improves their readability.
- C++ indentation is two spaces; Python indentation is four spaces.
- Run `just fmt` after editing code and `just fmt-check` to verify it. These use
  the repository's native clang-format and Ruff settings without custom syntax
  rewriting or mandatory expansion of short lists.
- Do not create or modify README files without an explicit request.
