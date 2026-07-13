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

- run 39 KEEP (mem 0.303, time 0.640): cost key carried in CostContext
  (register once per context, not per call) + read-only entry probe of the
  scheduled-set cache (sum-component calls hit it by construction);
  Pareto-dominates the run-37 trade-off.

- run 40 KEEP (mem 0.323, time 0.522): ALGORITHMIC — per-abstraction
  lookup table cache keyed by the cost function restricted to the
  abstraction's relevant operators (goal distances only depend on those);
  rovers06 ran 2.7M Dijkstras for 59 distinct tables before this.

- run 41 KEEP (mem 0.305, time 0.529): bit-packed cost blob pool with
  power-of-two widths (vectorizable pack, memcmp verify, one shared
  buffer); driverlog 933->649MB. Serial bit-shift packing and per-element
  unpack-verify variants cost +7-9% time and were rejected first.

- run 42 KEEP (mem 0.305, time 0.524): rolling content hash in
  CostContext updated in O(delta) during reduction (fused
  reduce_cost_context); removes the full-vector murmur per registration
  and recovers run 41's time cost.

- run 43 KEEP (mem 0.298, time 0.516): int16 LookupTableCache rows with
  an overflow map behind a sentinel, so arbitrarily large table id ranges
  stay supported.
- run 44 KEEP (mem 0.297, time 0.504): in-place context reduction with
  sparse per-frame undo (retry of run 22; economics flipped after the
  context gained hash/key and the call count dropped). CAUTION for future
  edits: the save buffer must be a per-frame local, not a member — the
  recursion re-enters create_max_node.

Consolidation audit (runs 45-49, "only keep what earns its keep"):
- run 45 KEEP: removed orphaned NodeKey/NodeKeyHash/PairUint32VectorIntHash/
  VectorUint8MurmurHash (dead since runs 38/41/42).
- run 46 ABLATION: live/cond context masks removed -> +2.7% time. Masks
  (runs 13/21) still earn their keep despite sparse conflict masks.
- run 47 ABLATION: static pair-conflict matrix removed -> +17% time.
  Run 36 earns its keep.
- run 48 ABLATION: single-component fast path removed -> +6% time.
  Run 18 earns its keep.
- run 49 ABLATION: read-only entry probe removed -> +14% time.
  Run 39 earns its keep.
Not ablated: shared-code changes benefiting scp_online too (runs 3, 12);
caches whose hit counters prove their value (runs 5, 17, 40); data
structure replacements where the previous version is strictly larger
(runs 28, 30, 31, 32); mechanisms validated against slower variants
during their own runs (41, 42); pure removals (14, 15, 25, 33, 45).
Final verified state: time 0.509, mem 0.297, all checks green.

- run 50 KEEP (clarity, neutral): split generator into sscp_dag.h +
  cost_function_registry.{h,cc}; sentinels unified; lookup_nodes renamed;
  mix_op_cost must stay inline in the header (hot loop).

Grid experiment (2026-07-12): experiments/2026-07-sscp-adhg/
2026-07-13-A-sscp-revisions.py compares 01-integration (edcb285f),
02-fast (ce63324b), 03-conflicts (142b8958), 04-table-sharing (42f02ccf),
05-final (a5255809) with sscp/systematic(2) on all optimal STRIPS
benchmarks (30min/8GiB, validated). Slurm jobs 53882271-76; report lands
in data/2026-07-13-A-sscp-revisions-eval/.

Zenodo artifact check (10.5281/zenodo.16606498, the code behind the
paper's experiments): precompute_conflicting_ops() and the on-the-fly path
both use set_intersection() of the scp-active operator sets, and comments
call relevant ops "saturation affecting labels". So paper AND published
artifact use the intersection; the union in the structured-scp GitHub
branch is a later regression (introduced when sorted-vector
set_intersection was rewritten to bit-vector operations: '|' where '&' was
meant). The Zenodo artifact also has NO handling for the -infinity
donation corner case; our donation terms are stricter than both.

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

- run 51 KEEP (robustness; probes neutral time 0.515/mem 0.297): budget for
  the restricted-cost table maps (grid regression fix, see below). Each
  abstraction's map stops growing at its share of a 256 MiB budget; if its
  hit count is below a quarter of the budget by then, sharing does not pay
  for this abstraction and the map is dropped. scanalyzer-08 p22 went from
  OOM at 8 GiB back to 26.7s / 404 MB (revision 03 level) while driverlog
  keeps the full table-sharing speedup.

## Grid experiment (2026-07-13, all optimal STRIPS, 30min/8GiB)

experiments/2026-07-sscp-adhg/2026-07-13-A-sscp-revisions.py, 5 revisions x
1827 tasks. Coverage: 01-integration 609, 02-fast 615, 03-conflicts 656,
04-table-sharing 649, 05-final 654. Geometric means over the 841 tasks
where every revision built the DAG: dag_time 0.326s -> 0.049s (6.6x),
search_start_time 0.630s -> 0.138s. Plan costs agree everywhere.

Two findings from the grid that probes had missed:

1. UPSTREAM SOUNDNESS BUG (gtl::bit_vector::find_next). On 28 tasks
   (barman, openstacks, pathways, psr, tpp, ...) 01-integration reports a
   HIGHER initial h than all later revisions (e.g. barman pfile01 10 vs 7,
   tpp p07 38 vs 10). Bisection -> run 13 (commit 5d71f13, OpMask rewrite)
   changed the values; per-pair instrumentation showed identical inputs but
   different conflict iteration: the ported branch iterates conflict sets
   with gtl::bit_vector::find_first/find_next, and find_next(start) misses
   set bits when start is not word-aligned (standalone repro: bit_vector(66),
   find_next(43) misses bit 65; 89k failures in a small sweep; vendored gtl
   copy in src/search/ext/gtl). Missed operators => spurious independence
   => the DAG sums over NON-order-independent components => h above the
   true max-over-orders (barman: 10 vs true 7 = value both revisions give
   with use_affecting_labels=false). Such h values are no SCP value of any
   order, i.e. potentially inadmissible; costs happened to stay optimal in
   the grid. Run 13 fixed this silently; 02..05 agree with the
   affecting-labels-off value everywhere. The Zenodo artifact
   (10.5281/zenodo.16606498) does NOT use gtl::bit_vector (it uses sorted
   vectors + set_intersection), so the paper's published code is affected
   by NEITHER this bug NOR the union/intersection bug; both are exclusive
   to the GitHub structured-scp branch. No other find_next user exists in
   our repo (per_state_bitset has its own packing).

2. TABLE-SHARING MEMORY BLOWUP (fixed by run 51). 04/05 lost
   scanalyzer-08 (4->0), scanalyzer-opt11 (1->0), tetris p02-4 and blocks
   p? vs 03: table_by_restricted_costs stores every distinct restricted
   cost vector; on scanalyzer ~all restricted vectors are unique
   (284k cost keys x 30 abstractions), so the maps ate >8 GiB for ~zero
   sharing while 03 solves p22 in 25s / 401 MB. Ablation (map disabled)
   reproduced 03's numbers exactly; run 51 bounds the maps.

Per-domain coverage vs 03-conflicts after the fix is expected at ~660
(654 + scanalyzer 5 + tetris 1); not re-run on the grid yet.

## Segment 2 (started 2026-07-13): peak memory during DAG generation

Grid analysis of 05-final's 1173 unsolved tasks: 605 OOM + 239 OOT die
DURING DAG generation (72%); only 311 die in search. On solved tasks DAG
time is negligible (median 1% of total). => The lever for coverage is peak
memory (and node growth) during construction, not construction speed.

Blowup profiles (memory ~ start_mem, i.e. all in construction):
- node explosion: parcprinter p02 58M nodes/5.9GB, psr p47 40M nodes,
  zenotravel p11 10M nodes, mprime prob08/17 2.6M nodes at 2.2-4.9GB.
- cost-function explosion: miconic s10-2 4.5M stored cost fns / 467k nodes.
- unexplained: snake04 7.0GB with only 220k nodes / 53k cost fns (6504 ops).

New probe suite (autoresearch.sh, ref2 = run-51 binary a3898cb9):
snake04 / freecell24 / mprime08 with bound=0; METRIC mem_ratio primary
(peak memory), time_ratio secondary. Old segment-1 suite preserved as
.autoresearch/time-suite.sh — run it as a guard before committing KEEPs.

gtl find_next bug reported upstream (reproduced on master v1.2.0, also
returns wrong indices: bit_vector(130) set(128), find_next(1) -> 129):
https://github.com/greg7mdp/gtl/issues/56

Segment 2 runs (metric: geomean peak-memory ratio on snake04/freecell24/
mprime08 vs run-51 reference; time_ratio secondary; segment-1 suite as
guard):
- run 52 KEEP (mem 0.988, time 0.96): sparse SaturatedCostFunction
  (parallel nonzero_ops/nonzero_costs, dense vector dropped, dead dense
  reduce_costs_unguarded removed). Snake SCFs turned out ~17-50% dense,
  so the win is small but consistent; also simpler.
- Aggregation fix: autoresearch.sh now geomeans per-probe ratios (a sum
  of peaks let mprime's 2.3GB drown snake04 wins).
- Sparsity measurement (temp instrumentation): 13-32% of operator costs
  differ from the ORIGINAL costs per registered function (snake 1121/6504,
  freecell 1002/3400, mprime 1363/10044, rovers 46/144). Sparse (op,value)
  delta lists would be LARGER than dense packs -> dead end. Diff BITMASK
  + packed changed values is strictly smaller -> run 53.
- run 53 KEEP (mem 0.903, time 0.93; guard improved to time 0.51 /
  mem 0.294): registry blobs stored as diff bitmask against the first
  registered cost function (the task costs) + bit-packed changed values.
  Registry was 86% of mprime08's 2.3GB peak and 46% of snake04 (massif).

- run 54 KEEP (mem 0.879): SSCPNode 32 -> 16 bytes via anonymous unions
  (children slice vs lookup table position; all accesses type-guarded).
  time_ratio read 0.98 at load ~50 - interleaved ratios compress toward 1
  under contention; re-verify when quiet.
- run 55 KEEP (mem 0.607!): chunked BlobPool for registry blobs. The one
  big vector's doubling realloc kept old+new copies live (83% of mprime08
  peak per massif). Guard mem +0.7% relative (reserved last-chunk tail on
  small tasks). Cumulative per-task peaks vs grid binaries:
  snake04 7.2GB -> 371MB, freecell24 2.2GB -> 246MB, mprime08 2.3GB -> 711MB.
- Dead end: interning diff MASKS (mask pool + set ids) - measured distinct
  masks == stored functions on snake/freecell/mprime (every function has a
  unique diff set), so interning cannot amortize anything.

- run 56 KEEP (probes neutral 0.6076; scanalyzer22 550->339MB at equal h):
  geometric hit-rate checkpoints (4096, doubling) drop useless
  restricted-cost caches long before the byte budget. First version had a
  bug: next_check=4096 exceeded max_entries floors (huge-entry tasks like
  snake04: 12KB/entry -> floor 1024) so maps grew unchecked to 3x memory;
  clamp next_check to the budget.
- run 57 KEEP (mem 0.4184 from 0.6076, time 0.98; guard mem 0.2816):
  restricted cost functions interned in per-abstraction
  CostFunctionRegistry instances (diff blobs, chunked pool) instead of
  vector<int> map keys (70% of freecell24 peak). Byte budget enforced on
  the registry's real footprint. Iterations that failed on the way:
  (a) fixed 4MiB first chunk per abstraction -> mem 0.68 (adaptive 64KiB
  ->4MiB doubling chunks fixed it, also helps small tasks);
  (b) pack-per-query cost 6% time, SWAR pack attempt 19% -> final design
  verifies hits with a one-pass mask-guided matches() against baseline
  (pack only on append) and hashes restricted queries with murmur (the
  incremental splitmix sum is only needed by the full-context registry).
  Segment 2 so far: mem 1.0 -> 0.42, guard suite mem 0.297 -> 0.282.

## Confirmation grid (2026-07-14-B, revision 06-memory = 1af42509)

Coverage 660 = new best (03-conflicts 656, 05-final 654, integration 609).
Initial h identical to 05-final on every common task (0 mismatches) -
exactness held through the entire memory segment. scanalyzer-08 4->4(+4
vs 05), scanalyzer-opt11 +1, tetris +1, snake +1, logistics00 -1;
vs 03-conflicts only gains (no domain lost). OOMs halved (774 -> 380),
failures shifted to out-of-time (381 -> 769): the frontier is time-bound
again - speed work on DAG generation pays from here. 3 sigkills =
organic-synthesis translator OOMs (same as previous grids).

Speed follow-ups after the confirmation grid (frontier now time-bound):
- Evidence: per-child compute_independent_abstractions returns a SINGLE
  component in 100% of calls on psr47 (45.8M calls, 26.5% of runtime) and
  mprime08; repeat rate of (cost key, remaining set): mprime 89%, psr 18%.
  Mask-change rate per reduction 67-95% => "masks unchanged" fast paths
  are dead ends.
- run 59 KEEP (time ~0.90, mem unchanged): probe the max-node memo with
  the unpartitioned remaining set BEFORE computing the partition; on a
  hit the partition is dead work and skipping it is exactly DAG-preserving
  (memoized sets never split at schedule time). psr47 unaffected (low
  repeat rate) - its 26.5% partition cost on 78 abstractions remains the
  top open speed item; partition memoization or faster merge detection
  are the next candidates.
- run 60 KEEP (psr47 dag 526s -> 382s, guard suite time 0.52 -> 0.452;
  segment-2 probes +3.5% accepted - they finish far under their limits
  while psr-class is the OOT frontier): frontier-search connected
  components replace the union-find pair loop; abstractions_depend() is
  the side-effect-free oracle. Reserve tweak was neutral; the +3.5% on
  the probe suite was not recovered (oracle constants), documented as a
  conscious trade.

Confirmation grid 2026-07-14-C (07-speed = 70a66a5d, runs 58-60):
coverage 661 (06-memory: 660), h identical everywhere, OOT 769 -> 747,
OOM 380 -> 401 (borderline churn; barman p435 flips at the 30min edge).
Coverage curve is flattening: remaining failures need either the
off-limits DAG-size work or larger speed factors.
- run 61 DISCARD: word-wise mask save/restore instead of the restore-path
  update_cost_context - neutral on probes and guard.
- run 62 KEEP (time 0.869 from 0.937, confidence 37x; guard 0.449):
  negative-scf/exhausted operators tracked as word masks during the sparse
  child reductions; simulation candidates = live & ~exhausted & ~negative
  gathered by bit scan (drops two O(ops) passes per max-node call). Legacy
  per-op scan kept for non-default label options.
