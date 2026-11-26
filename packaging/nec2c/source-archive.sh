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
cat > "$work/$name/BUILDING.txt" <<BUILDING
Building nec2c $version (commit $commit) from this archive
$(printf '%*s' 56 '' | tr ' ' '=')

nec2c/              the upstream source at the pinned commit
packaging/nec2c/    the build script, Windows compatibility headers and settings the NEC
                    Workbench packages were built with

No network access is needed. From this folder:

  Linux (gcc):
    packaging/nec2c/build.sh out nec2c

  Windows, in the MSYS2 MINGW64 shell (the x86_64-w64-mingw32 gcc, linked statically):
    packaging/nec2c/build.sh out nec2c

out/ then holds nec2c (or nec2c.exe) and nec2c-build.txt, the version, commit, compiler
and flags. The flags are those of the packaged build (-O2 -w, plus the MinGW-w64 settings
on Windows); CC picks another compiler. To run it in NEC Workbench, choose Configure… in
the engine list and browse to out/nec2c.
BUILDING
tar -czf "$out/$name.tar.gz" -C "$work" "$name"
echo "$out/$name.tar.gz"
