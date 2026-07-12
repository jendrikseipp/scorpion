# Autoresearch: structured SCP (sscp) speed, memory and clarity

## Objective

Improve the speed and memory usage of the newly integrated structured SCP
code (Höft et al., KR 2025) while keeping or improving code clarity. Only
keep changes that substantially improve at least one of speed, memory, or
clarity without degrading the others. The heuristic must stay *exact*: all
optimizations must preserve heuristic values bit-for-bit (checked by the
benchmark itself).

Integration commit: `edcb285f` ("Add structured SCP order generator and
heuristic"). The reference binary `.autoresearch/ref/downward` is built from
that commit and never changes.

## Metrics

- Primary: `time_ratio` = sum of candidate total times / sum of reference
  total times over the probe suite (lower is better; baseline ~1.0).
- Secondary: `mem_ratio` = sum of candidate peak memory / reference peak
  memory (lower is better). Track in every result line. A change that helps
  memory a lot at neutral time is a keep; judge via the same decide.py gates
  applied to mem_ratio samples.
- Clarity-only changes (dead code removal, simplification) are keeps when
  both ratios are neutral (decide.py DISCARD with ~0 change) AND the diff
  clearly reduces complexity. Note "clarity" in the description.

## How to run

- Benchmark: `./autoresearch.sh` (3 reps + warmup, ~4 min). Prints
  `METRIC time_ratio=...` and `METRIC mem_ratio=...` per rep. Non-zero exit
  = crash/correctness failure -> revert.
  It interleaves reference and candidate per probe on pinned core 4 and
  *enforces* equal initial h values and plan costs between the binaries.
- Checks: `./autoresearch.checks.sh` after a passing benchmark (optimal plan
  costs on two tasks; scp_online regression guard for shared code).
- Decision: `python3 /home/x_jense/.claude/skills/autoresearch/scripts/decide.py
  --best "<best kept samples>" --candidate "<samples>" --direction lower`
- Build candidate: `python3 build.py` (incremental).
- State: `autoresearch.jsonl` (append-only; samples = time_ratio,
  metrics.mem_ratio also stored).
- Commits: jj (colocated git). On KEEP: `jj describe -m "<desc>" && jj new`.
  On DISCARD: `jj restore && jj status` (working copy is always the
  experiment; parent = last kept commit). Never `git clean`.

## Probe suite (in .autoresearch/sas/)

| probe | task | config | ref time | ref mem |
|-------|------|--------|----------|---------|
| satellite_sys2 | satellite p07 | sscp full, systematic(2), bound=0 | ~4.7s | 56MB |
| elevators_mix | elevators p11 | sscp full, systematic(2)+cartesian, bound=0 | ~10.2s | 561MB |
| pipesworld_sys2 | pipesworld p08 | sscp full, systematic(2), bound=0 | ~13.1s | 71MB |
| driverlog_sys1_search | driverlog p11 | sscp full, systematic(1), full A* | ~0.5s | 59MB |

Sanity pre-check: elevators sys2 (h=28), mprime sys2 (h=3), both <0.1s.

Known-too-hard with systematic(2) (DAG explosion, >90s): depot, driverlog,
freecell, parcprinter, scanalyzer, tpp, woodworking — even small instances
(p04-p08). rovers p05/p06 also explode; rovers p07 works (54s search).
This scaling cliff is the main cost of the approach.

## Files in scope

- `src/search/cost_saturation/structured_scp_order_generator.{h,cc}`
- `src/search/cost_saturation/structured_scp_order_generator_full.{h,cc}`
- `src/search/cost_saturation/structured_scp_order_heuristic.{h,cc}`
- Supporting code they use: `cost_saturation/utils.{h,cc}` (compute_scf,
  compress_costs, hash functors), `algorithms/connected_components.{h,cc}`,
  the new methods in `projection.cc` / `explicit_abstraction.cc`.

## Off-limits

- `autoresearch.sh`, `autoresearch.checks.sh`, the reference binary, the
  SAS probe files.
- Anything that changes heuristic values or plan costs (the benchmark
  enforces this).
- Existing Scorpion behavior outside the sscp code paths (scp_online check
  guards this).

## Constraints

- Clean code matters: prefer simplifications; ugly complexity needs a big,
  confirmed win.
- Always interleaved A/B (built into autoresearch.sh).
- C++20, GCC 13.2. Build with `python3 build.py`.

## What's Been Tried

- run 2 DISCARD: hash-index for duplicate lookup-table detection in
  create_lookup_node — dedup scan is not a bottleneck (the Dijkstra before
  it is).
- run 3 KEEP (0.985): branchless reduce_costs{,_unguarded} so GCC
  vectorizes them (reduce_costs_unguarded had 15% self time). decide.py
  hovered at RERUN (baseline only has 3 samples); kept on 12/12 sign test.
- run 4 DISCARD: flipping cache_lookup_tables default alone = 40% SLOWER —
  compress_costs+hash per create_lookup_node call outweighs the tiny
  Dijkstras (systematic(2) has ~100-state abstractions).
- run 5 KEEP (0.775, 21% faster): register cost function once per max node,
  pass CostKey to create_lookup_node; cost cache keyed on raw vectors
  (compress_costs deleted); cache_lookup_tables default true. satellite had
  1.3M recomputed tables for 125 distinct ones.

- run 6 KEEP (0.758): incremental op_has_negative_scf flags per child in
  create_max_node instead of per-op all_of over all children.
- run 7 DISCARD: const-ref get_saturated_costs via deque cache — copies
  were not the cost.
- run 8 KEEP (0.620, 18%!): early exit in compute_independent_abstractions
  used the loop index instead of pending_abstraction_ids[i]; with the fix
  the O(n^2) pair loop stops once all pending abstractions are connected.
- run 9 KEEP (0.610): fuse reduce_costs_unguarded with negative-scf flags.
- run 10 DISCARD: sort-based get_connected_components + single-component
  fast path — within noise, longer code.
- run 11 DISCARD: simulated_any_op flag instead of vector compare — noise.
- run 12 KEEP (0.556): mutable pq/label_costs/applicable_operators members
  in Projection::compute_goal_distances (27k calls/task on satellite).

Insights:
- transitions=explicit for projections makes sscp construction ~38% faster
  on satellite at equal memory (config-level; probes pin implicit, so this
  is a user recommendation, maybe change the sscp default generator string).
- Lookup table cache stats on satellite after run 5: 1.27M hits, 27.7k
  misses (= real Dijkstras), 512 distinct cost functions.
- Profile (satellite sys2, post-run-8): 37% projection Dijkstra+match tree
  (+6% BucketQueue), 10% reduce_costs_unguarded, 10% create_max_node self,
  12% malloc/free/memmove.
