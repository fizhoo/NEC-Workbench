#!/usr/bin/env bash
# Runs a nec2c on every deck in a folder, the way Workbench does: in the deck's folder, with
# short relative file names (nec2c takes at most 75 characters).
# Usage: run-decks.sh <nec2c> <deck-directory> <output-directory>
set -euo pipefail
nec2c=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
out=$(mkdir -p "$3" && cd "$3" && pwd)
cd "$2"
count=0
for deck in *.nec; do
    name=${deck%.nec}
    "$nec2c" "-i$deck" "-o$name.out" > /dev/null
    mv "$name.out" "$out/"
    count=$((count + 1))
done
if [ "$count" -eq 0 ]; then
    echo "run-decks.sh: no decks in $2" >&2
    exit 1
fi
echo "$count decks run with $("$nec2c" -v)"
