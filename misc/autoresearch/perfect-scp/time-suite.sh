#!/bin/bash
# Interleaved A/B benchmark for the structured SCP (sscp) code.
#
# For each probe, runs the fixed reference binary (.autoresearch/ref/downward,
# built from the integration commit) and the candidate binary back to back on
# the same pinned core, and reports per-repetition ratios candidate/reference:
#
#   METRIC time_ratio=<x>   (primary, lower is better)
#   METRIC mem_ratio=<x>    (secondary, lower is better)
#
# Ratios cancel machine-wide drift; the reference never changes, so ratios are
# comparable across the whole experiment history.
#
# Correctness is enforced inline: initial heuristic values (and plan costs for
# search probes) must match between reference and candidate exactly. Any
# mismatch or crash exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
REF="$ROOT/.autoresearch/ref/downward"
CAND="$ROOT/builds/release/bin/downward"
SAS="$ROOT/.autoresearch/sas"
CORE=4
REPS=${REPS:-3}

SSCP_SYS1='astar(sscp(structured_order_generator=structured_order_generator_full(abstraction_generators=[projections(systematic(1))])))'
SSCP_SYS2_B0='astar(sscp(structured_order_generator=structured_order_generator_full(abstraction_generators=[projections(systematic(2))])),bound=0)'
SSCP_MIX_B0='astar(sscp(structured_order_generator=structured_order_generator_full(abstraction_generators=[projections(systematic(2)),cartesian()])),bound=0)'

# Probes: name / task / config. Segment 1 (memory focus): rovers06 is the
# main memory stressor (875MB, DAG-node dominated); satellite/gripper05/
# logistics cover construction time; driverlog covers search + correctness.
PROBE_NAMES=(satellite_sys2 gripper05_sys2 logistics_sys2 rovers06_sys2 driverlog_sys1_search)
PROBE_TASKS=(satellite gripper05 logistics rovers06 driverlog)
PROBE_CONFS=("$SSCP_SYS2_B0" "$SSCP_SYS2_B0" "$SSCP_SYS2_B0" "$SSCP_SYS2_B0" "$SSCP_SYS1")

# run BIN TASK CONF -> "total_time peak_mem h_init plan_cost"
run() {
    local bin=$1 task=$2 conf=$3 out status
    # Exit code 12 = search finished without a plan (expected for bound=0).
    status=0
    out=$(timeout 300 taskset -c $CORE "$bin" --search "$conf" \
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
    local ref_time=0 cand_time=0 ref_mem=0 cand_mem=0
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
        ref_time=$(python3 -c "print($ref_time + $rt)")
        cand_time=$(python3 -c "print($cand_time + $ct)")
        ref_mem=$((ref_mem + rm))
        cand_mem=$((cand_mem + cm))
    done
    if [[ "$report" == 1 ]]; then
        python3 -c "print(f'METRIC time_ratio={$cand_time / $ref_time:.4f}')"
        python3 -c "print(f'METRIC mem_ratio={$cand_mem / $ref_mem:.4f}')"
    fi
}

# Warm up file caches with one discarded repetition.
benchmark_rep 0 >/dev/null
for ((rep = 0; rep < REPS; ++rep)); do
    benchmark_rep 1
done
