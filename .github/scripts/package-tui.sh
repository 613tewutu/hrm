#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
  echo 'This release package targets Linux x86_64.' >&2
  exit 1
fi

source_binary="${1:-build/bundle/hrm-tui}"
release_dir=build/release
asset_name=hrm-tui-linux-x86_64-arch
staging_dir=$(mktemp -d)
trap 'rm -rf -- "$staging_dir"' EXIT

install -m 755 "$source_binary" "$staging_dir/hrm-tui"
strip --strip-unneeded "$staging_dir/hrm-tui"

# Run the exact stripped executable from a directory without game assets.
mkdir -p "$staging_dir/empty" "$staging_dir/config"
for level in {1..10}; do
  (
    cd "$staging_dir/empty"
    XDG_CONFIG_HOME="$staging_dir/config" "$staging_dir/hrm-tui" \
      --preview --no-audio --level "$level" --width 140 --height 52 > /dev/null
  )
done

mkdir -p "$release_dir"
install -m 755 "$staging_dir/hrm-tui" "$release_dir/$asset_name"
tar -czf "$release_dir/$asset_name.tar.gz" -C "$staging_dir" hrm-tui
(
  cd "$release_dir"
  sha256sum "$asset_name" "$asset_name.tar.gz" > SHA256SUMS
  sha256sum --check SHA256SUMS
)
