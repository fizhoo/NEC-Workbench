#!/usr/bin/env bash
# Fails unless a Windows executable imports only DLLs that come with Windows, so the bundled
# nec2c.exe runs with nothing else installed.
# Usage: check-imports.sh <executable>
set -euo pipefail
imports=$(objdump -p "$1" | awk '/DLL Name:/ { print $3 }')
status=0
for dll in $imports; do
    case "${dll,,}" in
        kernel32.dll | msvcrt.dll | ucrtbase.dll | api-ms-win-*) echo "system: $dll" ;;
        *) echo "not a Windows system DLL: $dll" >&2; status=1 ;;
    esac
done
[ -n "$imports" ] || { echo "check-imports.sh: no imports read from $1" >&2; exit 1; }
exit $status
