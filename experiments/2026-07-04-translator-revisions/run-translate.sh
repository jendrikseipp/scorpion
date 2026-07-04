#!/bin/bash
# Wrapper for the translator-revisions experiment.
#
# Args: BINARY DOMAIN_PDDL PROBLEM_PDDL
#
# Runs one revision's translate-cpp on the task under /usr/bin/time so peak
# memory (max RSS) is recorded uniformly for every revision, independent of the
# translator's own accounting. The translator writes output.sas into the run
# dir and prints its phase log + "Done! [...s CPU, ...s wall-clock]" +
# "Translator peak memory" to stdout for the parser. Virtual memory is capped
# at 8 GiB so a runaway revision is contained while its peak up to the cap is
# still recorded.
set -uo pipefail

BIN="$1"; DOMAIN="$2"; PROBLEM="$3"

(
    ulimit -v 8388608 2>/dev/null || true
    /usr/bin/time -v -o usrtime.log "$BIN" "$DOMAIN" "$PROBLEM"
) > translate.log 2>&1
status=$?

cat translate.log
echo "=== usrtime ==="
cat usrtime.log 2>/dev/null
echo "translate exit status: $status"
