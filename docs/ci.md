# GitHub CI and TUI releases

`.github/workflows/ci.yml` runs on branch pushes, pull requests, version-tag
pushes, and manual dispatch. The build/test job runs in an Arch Linux container
on a GitHub-hosted x86_64 runner, matching the project's rolling-release tools.
It checks workflow/shell syntax, native formatting, C++11 kernel and OJ parity,
application/level regressions, procedural synthesis, standalone resource loading, and real terminal
mouse/keyboard behavior. Audio tests do not open a sound device.
Application and audio test targets run from the project root, keeping source-YAML
checks and relative audition-export paths correct in clean builds.

The workflow configures `release` mode with GUI disabled. It packages the exact
tested TUI with embedded default levels and procedural audio. The packaging
script strips a temporary copy, smoke-tests all ten levels from an empty
directory, and emits these files under `build/release/`:

- `hrm-tui-linux-x86_64-arch`: a standalone project-resource executable.
- `hrm-tui-linux-x86_64-arch.tar.gz`: the same executable named `hrm-tui`, preserving its mode.
- `SHA256SUMS`: checksums for both download formats.

Every successful run uploads these files as a workflow artifact for 14 days.
Pushing a tag such as `v0.1.0` also creates a GitHub Release after the checks pass.
Tags containing a hyphen, such as `v0.1.0-rc.1`, create prereleases. Rerunning a
tag workflow updates its existing assets. The release job verifies checksums
and uses the built-in GitHub token; it does not rebuild the binary or need a PAT.
Manual dispatch runs checks and uploads artifacts without publishing a release.

Only the tag-gated publishing job has `contents: write`; build/test jobs use
read-only repository access. Actions are pinned to official release commit SHAs,
and xmake's installed packages are cached separately from the working checkout.

The release is an Arch-built Linux x86_64 binary and requires compatible system
C/C++ and raylib platform libraries, as described in [distribution.md](distribution.md).
It is not a fully static or cross-platform release. After extracting the tarball,
run `./hrm-tui`; for the raw executable, set its executable bit first if necessary.
Verify downloads with `sha256sum --check SHA256SUMS` when both formats are present.

Locally, `just bundle-tui` followed by `bash .github/scripts/package-tui.sh`
exercises the same packaging path. `gh release create --verify-tag` checks that
the version tag already exists; see the [GitHub CLI reference](https://cli.github.com/manual/gh_release_create).
