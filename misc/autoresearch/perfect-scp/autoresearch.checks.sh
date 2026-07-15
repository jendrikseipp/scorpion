#!/bin/bash
# Correctness checks for the sscp autoresearch loop, run off the metric clock
# after a passing benchmark. A failure reverts the experiment like a crash.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="$ROOT/builds/release/bin/downward"
SAS="$ROOT/.autoresearch/sas"

fail=0

check() {
    local desc=$1 task=$2 conf=$3 pattern=$4 expected=$5 out actual status
    # Exit code 12 = search finished without a plan (expected for bound=0).
    status=0
    out=$(timeout 300 "$BIN" --search "$conf" < "$SAS/$task.sas" 2>&1) \
        || status=$?
    if [[ $status != 0 && $status != 12 ]]; then
        echo "CHECK CRASHED ($status): $desc"
        echo "$out" | tail -5
        fail=1
        return 0
    fi
    actual=$(grep -oP "$pattern" <<<"$out" || echo MISSING)
    if [[ "$actual" != "$expected" ]]; then
        echo "CHECK FAILED: $desc: got $actual, expected $expected"
        fail=1
    else
        echo "check ok: $desc"
    fi
}

# Full searches with sscp must find the known optimal plan costs.
check "driverlog sys1 optimal cost" driverlog \
    'astar(sscp(structured_order_generator=structured_order_generator_full(abstraction_generators=[projections(systematic(1))])))' \
    'Plan cost: \K\d+' 19
check "elevators sys2 optimal cost" elevators \
    'astar(sscp(structured_order_generator=structured_order_generator_full(abstraction_generators=[projections(systematic(2))])))' \
    'Plan cost: \K\d+' 56

# Guard the shared code paths (projections, cost saturation utils) used by
# the established scp_online heuristic.
check "scp_online initial h unchanged" satellite \
    'astar(scp_online([projections(systematic(2))]),bound=0)' \
    'Initial heuristic value for scp_online: \K\d+' 9

exit $fail
