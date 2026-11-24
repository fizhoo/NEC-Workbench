#!/usr/bin/env bash
# Packs the bundled nec2c's corresponding source: the pinned upstream commit, and this
# folder's build script, Windows compat headers and build settings.
# Usage: source-archive.sh <output-directory> <source-directory>
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(mkdir -p "$1" && cd "$1" && pwd)
commit=$(tr -d '[:space:]' < "$here/commit")
version=$(tr -d '[:space:]' < "$here/version")
name=nec2c-$version-${commit:0:7}-source
work=$(mktemp -d)
git -C "$2" archive --format=tar --prefix="$name/nec2c/" "$commit" | tar -x -C "$work"
mkdir -p "$work/$name/packaging"
cp -R "$here" "$work/$name/packaging/nec2c"
cp "$here/../../THIRD_PARTY_LICENSES.txt" "$work/$name/"
tar -czf "$out/$name.tar.gz" -C "$work" "$name"
echo "$out/$name.tar.gz"
