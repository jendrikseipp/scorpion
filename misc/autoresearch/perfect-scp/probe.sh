#!/bin/bash
# Probe: run sys_scp on each task, report evals + time breakdown.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="${1:-$ROOT/builds/release/bin/downward}"
SAS="$ROOT/.autoresearch/sas"
CONF='astar(scp_online([projections(sys_scp(max_time=5, max_time_per_restart=1))]), bound=0)'
for sas in "$SAS"/*.sas; do
    name=$(basename "$sas" .sas)
    out=$("$BIN" --search "$CONF" < "$sas" 2>&1)
    evals=$(grep -oP 'Ordered systematic pattern evaluations: \K\d+' <<<"$out" || echo NA)
    pat_t=$(grep -oP 'computing ordered systematic patterns: \K[\d.]+' <<<"$out" || echo NA)
    proj_t=$(grep -oP 'evaluating ordered systematic projections: \K[\d.]+' <<<"$out" || echo NA)
    sel=$(grep -oP 'Selected ordered systematic patterns: \K\S+' <<<"$out" || echo NA)
    maxsize=$(grep -oP 'Maximum generated ordered systematic pattern size: \K\d+' <<<"$out" || echo NA)
    stop=$(grep -oE 'Reached (overall time limit|maximum collection size|maximum number of patterns)|Generated all patterns up to size [0-9]+|Restart did not add any pattern' <<<"$out" | tail -1)
    echo "$name evals=$evals gen_time=$pat_t eval_time=$proj_t selected=$sel max_size=$maxsize stop='$stop'"
done
