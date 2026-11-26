#!/usr/bin/env bash
# Builds the bundled nec2c from the pinned commit (commit, version) with a plain compiler run.
# Usage: build.sh <output-directory> <source-directory>
# The source directory receives the pinned checkout and is kept for the reference build and
# the source archive. A git checkout there is moved to the pinned commit; an exported source
# tree (the nec2c folder of the release source archive) is built as it is, offline; only an
# empty or missing directory is cloned. CC picks the compiler (gcc by default); a Windows build must use the
# MinGW-w64 gcc (x86_64-w64-mingw32), never MSYS gcc, and links statically.
# Writes nec2c (or nec2c.exe) and nec2c-build.txt, the build metadata Workbench shows.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$(mkdir -p "$1" && cd "$1" && pwd)
source=$2
commit=$(tr -d '[:space:]' < "$here/commit")
version=$(tr -d '[:space:]' < "$here/version")
cc=${CC:-gcc}
if [ -d "$source/.git" ]; then
    git -C "$source" -c advice.detachedHead=false checkout --quiet "$commit"
elif [ ! -f "$source/main.c" ]; then
    git clone --quiet https://github.com/KJ7LNW/nec2c "$source"
    git -C "$source" -c advice.detachedHead=false checkout --quiet "$commit"
fi

# No -ffast-math or -march: the bundled build must compute what any plain build computes.
flags=(-O2 -w)
machine=$("$cc" -dumpmachine)
executable=nec2c
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        if [ "$machine" != x86_64-w64-mingw32 ]; then
            echo "build.sh: use the MINGW64 gcc (x86_64-w64-mingw32), not $machine" >&2
            exit 1
        fi ;;
esac
if [ "$machine" = x86_64-w64-mingw32 ]; then
    # The C99 printf, so numbers print as on Linux (two-digit exponents), and the compat
    # headers for the POSIX calls MinGW lacks.
    flags+=(-static -D__USE_MINGW_ANSI_STDIO=1 -I"$here/compat")
    executable=nec2c.exe
fi
short=${commit:0:7}
sources=()
for name in calculations geometry input matrix network shared fields ground main misc radiation somnec; do
    sources+=("$source/$name.c")
done
"$cc" "${flags[@]}" "-DPACKAGE_STRING=\"nec2c $version ($short)\"" "${sources[@]}" \
    -o "$out/$executable" -lm
cat > "$out/nec2c-build.txt" <<METADATA
name=nec2c
version=$version
commit=$commit
source=https://github.com/KJ7LNW/nec2c/tree/$commit
compiler=$("$cc" --version | head -n 1)
target=$machine
flags=${flags[*]//$here/packaging/nec2c}
METADATA
cat "$out/nec2c-build.txt"
