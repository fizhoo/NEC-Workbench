#!/usr/bin/env bash
# Runs a CI step's command, showing its output, and on failure repeats the last lines as an
# error annotation, which stays readable on the run's summary page. With CI_STEP_NOTICE=1 it
# repeats them as a notice when the step passes too, for results worth reading.
# Usage: ci-step.sh <title> <command> [arguments...]
set -uo pipefail
title=$1
shift
log=$(mktemp)
"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
lines=$(tail -n 60 "$log" | sed -e 's/%/%25/g' | sed -e ':a;N;$!ba;s/\n/%0A/g')
if [ "$status" -ne 0 ]; then
    echo "::error title=$title::$lines"
elif [ "${CI_STEP_NOTICE:-}" = 1 ]; then
    echo "::notice title=$title::$lines"
fi
exit "$status"
