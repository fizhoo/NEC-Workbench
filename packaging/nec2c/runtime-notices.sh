#!/usr/bin/env bash
# Appends the license notices of the MinGW-w64 runtime pieces linked statically into
# nec2c.exe (the C runtime start-up code, the GCC runtime library, winpthreads) to a copy of
# THIRD_PARTY_LICENSES.txt. Run in the MSYS2 MINGW64 shell that built nec2c.exe.
# Usage: runtime-notices.sh <THIRD_PARTY_LICENSES.txt to append to>
set -euo pipefail
licenses=${MINGW_PREFIX:-/mingw64}/share/licenses
notices=$(find "$licenses" -type f \( -path '*crt*' -o -path '*gcc-libs*' -o -path '*winpthread*' \) | sort)
if [ -z "$notices" ]; then
    echo "runtime-notices.sh: no MinGW-w64 runtime notices under $licenses; found:" >&2
    find "$licenses" -maxdepth 2 >&2
    exit 1
fi
for notice in $notices; do
    echo "added ${notice#"$licenses"/}"
    printf '\n\nMinGW-w64 runtime notice: %s\n%s\n\n' "${notice#"$licenses"/}" \
        "----------------------------------------------------------------" >> "$1"
    cat "$notice" >> "$1"
done
