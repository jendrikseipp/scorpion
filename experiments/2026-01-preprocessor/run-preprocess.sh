#!/bin/bash
# Wrapper for the preprocessor-revisions experiment.
#
# Args: FD_DRIVER BINARY H2FLAG DOMAIN_PDDL PROBLEM_PDDL
#
# Translates the task with a SINGLE fixed translator (FD_DRIVER, the experiment's
# checkout) so every revision's preprocessor gets byte-identical input, then runs
# that revision's preprocess-h2 under /usr/bin/time to record peak memory (max
# RSS) uniformly -- including revisions that do not self-report it. Output goes
# to stdout for the parser. Memory is capped at 8 GiB so runaway revisions are
# contained while still recording their peak up to the cap.
set -uo pipefail

FD="$1"; BIN="$2"; H2FLAG="$3"; DOMAIN="$4"; PROBLEM="$5"

echo "=== translate (fixed translator) ==="
python3 "$FD" --sas-file output.sas --translate "$DOMAIN" "$PROBLEM" > translate.log 2>&1 \
    || { echo "TRANSLATE_FAILED"; tail -5 translate.log; exit 1; }

echo "=== preprocess: $BIN $H2FLAG ==="
(
    ulimit -v 8388608 2>/dev/null || true
    /usr/bin/time -v -o usrtime.log "$BIN" --outfile preprocessed.sas "$H2FLAG" 9999 < output.sas
) > preprocess.log 2>&1
status=$?

cat preprocess.log
echo "=== usrtime ==="
cat usrtime.log 2>/dev/null
echo "preprocess exit status: $status"
