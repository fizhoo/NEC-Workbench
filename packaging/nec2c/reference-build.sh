#!/usr/bin/env bash
# Builds nec2c a second way, with upstream's own autotools and the same flags as build.sh,
# from a pinned checkout made by build.sh. Its output must match the bundled build's.
# Usage: reference-build.sh <output-directory> <source-directory>
set -euo pipefail
out=$(mkdir -p "$1" && cd "$1" && pwd)
work=$(mktemp -d)
cp -R "$2/." "$work"
cd "$work"
autoreconf --install --force > /dev/null 2>&1
./configure --quiet CFLAGS="-O2 -w" > /dev/null
make --quiet > /dev/null
cp nec2c "$out/nec2c"
"$out/nec2c" -v
