# Autoresearch: structured SCP (sscp) speed, memory and clarity

## SEGMENT 1 (current, since 2026-07-12): MEMORY focus

User steer: focus on memory improvements that do not hurt speed; clean
code. New probe suite (rovers06 added as the 875MB memory stressor;
elevators_mix and pipesworld dropped) and the reference binary was
re-frozen at the run-25 commit (ce63324b), so both ratios restart at ~1.0.

Decision rule for segment 1:
- Primary: decide.py on mem_ratio samples (direction lower).
- Guard: time_ratio must not regress: discard if the time median worsens
  by more than ~1.5% with decide-style confidence.
- Clarity keeps as before (both ratios neutral + clearly simpler diff).

rovers06 memory anatomy (875MB peak, 32 abstractions, ops~1600?):
2.88M DAG nodes (2.72M sum + 156k max), only 59 lookup tables, 156k stored
cost functions (cost_key_cache holds FULL cost vectors), 2.88M
instructions. Main consumers: cost_key_cache (~300MB est.), DAG nodes +
dedup-cache key vectors, instructions.

## Segment 1 experiment log

- run 26: segment baseline (identical binaries, ref = run-25 commit).
- run 27 KEEP (mem 0.943, time +0.9%): packed cost function storage;
  raw-vector hash for identification, byte-packed blobs (1/2/4 bytes per
  cost) for verification, overflow list for true hash collisions. NOTE:
  packing in the lookup hot path (bit-granular or without scratch reuse)
  cost 2-3.5% time; the raw-hash + packed-verify split is what works.
- run 28 KEEP (mem 0.853, time 0.944): sum/max post caches as hash SETS of
  the nodes with transparent hash/eq on children indices — no key-vector
  copy per entry (2.9M entries on rovers06). Memory AND speed win.
- run 29 KEEP (mem 0.806, time 0.957): free construction caches before the
  instruction phase; release each node's children right after its
  instruction is built.
- run 30 KEEP (mem 0.760, time 0.943): flattened Instructions struct
  (types/id_offsets/ids shared buffers) instead of a vector per
  instruction; also tightens the evaluation loop.
- run 31 KEEP (mem 0.644, time 0.79): flat arena of SSCPNode structs with
  integer node ids replaces the shared_ptr hierarchy (no vptr/control
  blocks/dynamic_pointer_cast; also fixes never-reset static counters).
  -15% memory and 16% faster in one change.
- run 32 KEEP (mem 0.529, time 0.777): shared children pool with per-node
  slices (children_offset/num_children) instead of a vector per node.
  Segment total so far: -47% memory, -22% time on the memory suite.
- run 33 KEEP (clarity, neutral): drop the node level field; arena order
  is topological (children created before parents), so sorting reachable
  nodes by id replaces the level sort.

Remaining memory ideas (diminishing returns on this suite): pack SSCPNode
fields via union (~5%), intern the id-set vectors in the max pre-cache,
malloc fragmentation. The DAG size itself remains the frontier on hard
tasks.

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
- run 13 KEEP (0.539): uint64 OpMask word masks + per-call live/cond
  operator classification for dependency checks; drops gtl::bit_vector.
- run 14 KEEP (clarity): remove dump_tree/dump_node, collect_time
  option+timers, dead compute_remaining_costs (-85 lines, neutral).
- run 15 KEEP (clarity): StructuredSCPOptions struct replaces the
  g_hacked_* globals (neutral).
- run 16 DISCARD: hoist per-child buffers in create_max_node — 2% worse.
- run 17 KEEP (0.259, 52%!): cache PRUNED_LOOKUP sentinel for all-zero /
  dead-end-only lookup outcomes — those Dijkstras were recomputed for every
  recurrence of the same cost function. Overall now 3.9x faster than the
  integration baseline.

- run 18 KEEP (0.252): retry of run 10's single-component fast path in
  get_connected_components — worthwhile after run 17 shifted the profile.

- run 19 KEEP (0.237): SaturatedCostFunction struct with nonzero_ops;
  sparse reduce loops in create_max_node (SCFs of small projections are
  mostly zeros).

- run 20 KEEP (0.196, 17%): deque scf_cache + const-ref
  get_saturated_costs, no per-child SCF copies (successful retry of run 7
  after run 19 made the copies heavier). 5.1x faster than integration.
- run 21 KEEP (0.191): CostContext with incrementally maintained live/cond
  masks threaded through the DAG construction (no O(ops) mask rebuild per
  compute_independent_abstractions call). 5.2x faster than integration.
- run 22 DISCARD: in-place CostContext mutation with undo (backtracking)
  instead of per-child copies — 1.6% worse and hurts clarity.

- run 23 KEEP (0.188, mem 0.997): dense [cost_key][abstraction] lookup
  cache instead of per-abstraction hash maps; kept on sign test.

- run 24 DISCARD: reusable member DisjointSet with sparse reset — no win,
  the per-call allocations were not significant.
- run 25 KEEP (clarity): non-const Instruction members (restores vector
  moves) and integer max_lookup_table_entries; metric neutral (0.189).

IMPORTANT for resuming: on DISCARD use `jj restore src` (NOT bare
`jj restore`), otherwise uncommitted autoresearch.md updates are clobbered.

Not worth it (analyzed, skipped): incremental Zobrist hashing for cost keys
(saves ~4% murmur but needs collision verification + threading; ugly
complexity for a small gain).

ALGORITHMIC runs (user steer: attack DAG size):
- run 34 KEEP (mem 0.324, time 0.926): intersection-based conflicts +
  inf-donation terms (implements Theorems 6/7 of the paper; ported branch
  had union = bug). rovers06 DAG 2.88M -> 14.6k nodes; tpp06 from 7GB
  blowup to 0.007s / 87 nodes (optimal cost verified vs lmcut).
- run 35 DISCARD: input-keyed memoization with NodeKey vectors (mem 2x);
  first attempt without the scheduled-key layer re-exploded (satellite
  timeout) — different inputs prune to the same scheduled set, the
  scheduled-set cache is what collapses the exponent.
- run 36 KEEP (time 0.926 -> 0.794): static pair-conflict bit matrix
  skips statically independent pairs in the O(n^2) loops.
- run 37 DISCARD (trade-off): interned-set memoization incl. input keys:
  time 0.65 but mem 0.43. Noted as a time-leaning alternative.
- run 38 KEEP (mem 0.303, time 0.786): scheduled-set cache keyed by packed
  (cost key, interned set id).

Paper analysis (Höft, Speck & Seipp, KR 2025) of union vs intersection:
- Theorem 6 (SCP*-AFF): h1, h2 are order-independent if the sets
  L^non-goal (labels with a transition between distinct states, at most
  one a goal state) are DISJOINT, i.e., independence iff INTERSECTION of
  the scp-active label sets is empty. Theorem 7 (SCP*-AFF-inf) removes
  labels with cost(l)=inf from the intersection. operator_is_scp_active()
  in the code is exactly L^non-goal.
- The ported branch's union (rel1 | rel2) therefore does NOT implement the
  paper; it made almost all pairs dependent and disabled the AFF rule
  (only the inf rule fired). Bug in the ported code, not the paper's
  theory and not the new code. The paper's reported 4-orders-of-magnitude
  ADHG reductions match what the intersection rule gives us (rovers06:
  2.88M -> 14.6k nodes).
- One genuine corner case the paper's Lemma 1/Theorem 6 gloss over: under
  general cost partitioning, an operator with NO transition in an
  abstraction has mscf = -infinity (not saturation-affecting), so
  saturating that abstraction donates infinite remaining cost to later
  heuristics, which can change their values. Cannot happen for projections
  (irrelevant operators self-loop); rare for Cartesian/explicit
  abstractions. run 34 adds explicit donation terms to the conflict masks
  to stay exactly value-preserving in this case.

Insights:
- Hard domains (driverlog, woodworking, scanalyzer even small instances)
  remain infeasible after 5x speedup: the DAG itself explodes (2.3GB on
  driverlog p11 sys2). Constant factors do not move that cliff; memory of
  DAG nodes is the frontier for hard instances.
- transitions=explicit for projections makes sscp construction ~38% faster
  on satellite at equal memory (config-level; probes pin implicit, so this
  is a user recommendation, maybe change the sscp default generator string).
- Lookup table cache stats on satellite after run 5: 1.27M hits, 27.7k
  misses (= real Dijkstras), 512 distinct cost functions.
- Profile (satellite sys2, post-run-8): 37% projection Dijkstra+match tree
  (+6% BucketQueue), 10% reduce_costs_unguarded, 10% create_max_node self,
  12% malloc/free/memmove.
