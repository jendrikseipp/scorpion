#!/bin/bash
# Interleaved A/B benchmark for the structured SCP (sscp) code, segment 2:
# peak memory during DAG generation on blowup tasks.
#
# The grid experiment showed that 72% of unsolved tasks die during DAG
# generation (605 of them out of memory), so this segment's probes are
# construction-bound blowup tasks run with bound=0:
#
#   snake04    (~38s, 7.0GB peak, 220k nodes but huge memory)
#   freecell24 (~60s, 2.1GB peak, 454k nodes)
#   mprime08   (~74s, 2.2GB peak, 2.6M nodes; node-explosion profile)
#
# For each probe, runs the fixed reference binary (.autoresearch/ref2/downward,
# built from commit a3898cb9 = run 51) and the candidate binary back to back
# on the same pinned core and reports per-repetition ratios candidate/reference:
#
#   METRIC mem_ratio=<x>    (primary, lower is better; peak memory)
#   METRIC time_ratio=<x>   (secondary, lower is better)
#
# Before committing a KEEP, also run the segment-1 time suite as a guard:
#   bash misc/autoresearch/perfect-scp/time-suite.sh   (time_ratio must not regress)
#
# Correctness is enforced inline: initial heuristic values must match between
# reference and candidate exactly. Any mismatch or crash exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
REF="$ROOT/.autoresearch/ref2/downward"
CAND="$ROOT/builds/release/bin/downward"
SAS="$ROOT/.autoresearch/sas"
CORE=4
REPS=${REPS:-3}

SSCP_SYS2_B0='astar(sscp(structured_order_generator=structured_order_generator_full(abstraction_generators=[projections(systematic(2))])),bound=0)'

PROBE_NAMES=(snake04 freecell24 mprime08)
PROBE_TASKS=(snake04 freecell24 mprime08)
PROBE_CONFS=("$SSCP_SYS2_B0" "$SSCP_SYS2_B0" "$SSCP_SYS2_B0")

# run BIN TASK CONF -> "total_time peak_mem h_init plan_cost"
run() {
    local bin=$1 task=$2 conf=$3 out status
    # Exit code 12 = search finished without a plan (expected for bound=0).
    status=0
    out=$(timeout 600 taskset -c $CORE "$bin" --search "$conf" \
              < "$SAS/$task.sas" 2>&1) || status=$?
    if [[ $status != 0 && $status != 12 ]]; then
        echo "RUN FAILED ($status): $task" >&2
        echo "$out" | tail -5 >&2
        return 1
    fi
    local tt mem h cost
    tt=$(grep -oP 'Total time: \K[\d.]+' <<<"$out")
    mem=$(grep -oP 'Peak memory: \K\d+' <<<"$out")
    h=$(grep -oP 'Initial heuristic value for sscp: \K\d+' <<<"$out")
    cost=$(grep -oP 'Plan cost: \K\d+' <<<"$out" || echo -)
    echo "$tt $mem $h $cost"
}

# Fast sanity pre-check (<1s): trivial tasks with known initial h values.
sanity() {
    local res h
    for spec in "elevators:28" "mprime:3"; do
        local task=${spec%%:*} expected=${spec##*:}
        res=$(run "$CAND" "$task" "$SSCP_SYS2_B0")
        h=$(cut -d' ' -f3 <<<"$res")
        if [[ "$h" != "$expected" ]]; then
            echo "SANITY FAILED: $task h=$h expected=$expected" >&2
            exit 1
        fi
    done
}

sanity

benchmark_rep() {
    local report=$1  # 1 = print METRIC lines, 0 = warmup
    # Geometric mean of per-probe ratios: every probe counts equally, so a
    # win on a small task is not drowned by a larger one (each grid task
    # has its own memory limit).
    local time_ratios=() mem_ratios=()
    for i in "${!PROBE_NAMES[@]}"; do
        local task=${PROBE_TASKS[$i]} conf=${PROBE_CONFS[$i]}
        local r c
        r=$(run "$REF" "$task" "$conf")
        c=$(run "$CAND" "$task" "$conf")
        read -r rt rm rh rc <<<"$r"
        read -r ct cm ch cc <<<"$c"
        if [[ "$rh" != "$ch" || "$rc" != "$cc" ]]; then
            echo "CORRECTNESS FAILED: ${PROBE_NAMES[$i]}:" \
                 "h $rh vs $ch, cost $rc vs $cc" >&2
            exit 1
        fi
        time_ratios+=("$ct/$rt")
        mem_ratios+=("$cm/$rm")
    done
    if [[ "$report" == 1 ]]; then
        python3 -c "
from math import prod
mem = [${mem_ratios[0]}, ${mem_ratios[1]}, ${mem_ratios[2]}]
time = [${time_ratios[0]}, ${time_ratios[1]}, ${time_ratios[2]}]
print(f'METRIC mem_ratio={prod(mem) ** (1 / len(mem)):.4f}')
print(f'METRIC time_ratio={prod(time) ** (1 / len(time)):.4f}')
"
    fi
}

# Warm up file caches with one discarded repetition.
benchmark_rep 0 >/dev/null
for ((rep = 0; rep < REPS; ++rep)); do
    benchmark_rep 1
done
