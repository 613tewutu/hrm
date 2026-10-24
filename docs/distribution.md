# Single-file TUI resource bundle

Run `just bundle-tui` to build and copy the TUI to `build/bundle/hrm-tui`.
Pass a different output path with `just bundle-tui path/to/hrm-tui`.
Only the executable needs to be distributed as project content:

- All ten default levels are embedded at build time from `assets/game.yaml`.
- The music score and sound synthesis are compiled into the shared audio module.
- No external YAML, WAV, font, or sprite files are needed for the TUI.

The YAML remains the single authoring source. Xmake's native `utils.bin2c`
rule generates a null-terminated byte array in the build directory and tracks
the YAML as an input. `load_bundled_config()` validates the embedded data with
the same parser used for external configurations. Generated headers are not
committed or maintained separately.

Default startup always uses embedded levels, even if `assets/game.yaml` exists
in the working directory. `--config PATH` opts into an external custom game.
The first three OJ levels and C++11 submission remain unchanged.

Player solutions are writable user data and still live in
`$XDG_CONFIG_HOME/hrm/` or `~/.config/hrm/`. The executable never needs to write
beside itself. Rebuilding default resources does not change completion for
levels whose execution data remain identical.

This is a resource bundle, not a fully static Linux executable. The existing
system C/C++ runtime and raylib platform libraries are still dynamically linked;
use a compatible Linux system. No standalone FTXUI, yaml-cpp, or raylib library
files are copied alongside the executable in the current static-package build.

Run `just test-bundle` to test the actual copied artifact in temporary directories.
It loads and renders every default level with no adjacent assets, ignores a
malformed local YAML by default, rejects that YAML when explicitly selected,
and loads a valid custom configuration with `--config`. Device initialization
is disabled for previews. `just test-tui` additionally solves levels 6–10 with
real programs and edge cases, and checks bundled/source YAML parity.
