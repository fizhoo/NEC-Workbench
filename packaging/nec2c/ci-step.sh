#!/usr/bin/env bash
# Runs a CI step's command, showing its output, and on failure repeats the last lines as an
# error annotation, which stays readable on the run's summary page.
# Usage: ci-step.sh <title> <command> [arguments...]
set -uo pipefail
title=$1
shift
log=$(mktemp)
"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
if [ "$status" -ne 0 ]; then
    echo "::error title=$title::$(tail -n 60 "$log" | sed -e 's/%/%25/g' | sed -e ':a;N;$!ba;s/\n/%0A/g')"
fi
exit "$status"
