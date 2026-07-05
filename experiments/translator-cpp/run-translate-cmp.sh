#!/bin/bash
# Wrapper for the Python-vs-C++ translator comparison.
#
# Args: DRIVER IMPL DOMAIN_PDDL PROBLEM_PDDL   (IMPL is "py" or "cpp")
#
# Runs one translator via the driver under /usr/bin/time so peak memory
# (max RSS) is uniform across both, writes output.sas, and prints its sha256
# so byte-equivalence between py and cpp can be checked per task. The
# translator's own "Done! [<cpu>s CPU, ...]" line gives a driver-free runtime.
# Virtual memory is capped at 8 GiB.
set -uo pipefail

DRIVER="$1"; IMPL="$2"; DOMAIN="$3"; PROBLEM="$4"

(
    ulimit -v 8388608 2>/dev/null || true
    /usr/bin/time -v -o usrtime.log python3 "$DRIVER" --translate \
        --translator "$IMPL" --sas-file output.sas "$DOMAIN" "$PROBLEM"
) > driver.log 2>&1
status=$?

cat driver.log
echo "=== usrtime ==="
cat usrtime.log 2>/dev/null
if [ -f output.sas ]; then
    echo "SAS_SHA256 $(sha256sum output.sas | cut -d' ' -f1)"
fi
echo "translate exit status: $status"
