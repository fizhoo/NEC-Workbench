#!/usr/bin/env bash
# Downloads the source tarballs of the given Qt modules for an exact Qt version, unless they
# are already there, for packaging/qt-notices.py.
# Usage: qt-sources.sh <qt-version> <directory> <module>...
set -euo pipefail
version=$1 folder=$2
shift 2
series=$(cut -d. -f1-2 <<< "$version")
mkdir -p "$folder"
for module in "$@"; do
    file="$folder/$module-everywhere-src-$version.tar.xz"
    [ -s "$file" ] && continue
    curl -sSfL -o "$file" "https://download.qt.io/archive/qt/$series/$version/submodules/$(basename "$file")"
    ls -l "$file"
done
