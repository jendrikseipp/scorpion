# Idea backlog for sscp speed/memory/clarity

Seeded from reading the ported code; ordered roughly by expected value.

## Speed

- `create_lookup_node`: linear scan over all existing lookup tables of an
  abstraction comparing full goal-distance vectors (`goal_distances ==
  lookup_table`). Replace with a hash of the vector -> id map (upstream TODO
  comments hint at this). Should help construction-heavy probes directly.
- `cache_lookup_tables` defaults to false; the compressed-costs cache path
  might be cheaper than recomputing Dijkstra. Try enabling by default /
  measuring; or make the cost compression cheaper (hash raw costs vector
  directly instead of bit-packing first).
- `compute_independent_abstractions`: O(n^2) find() pairs each call; called
  once per scheduled child per max node. Consider early exit when all in one
  set (already there via goto), caching components per (cost_key, ids).
- `get_saturated_costs` returns Costs *by value* from scf_cache (copy of a
  full operator-cost vector per call). Return const ref where possible;
  `compute_remaining_costs` copies again.
- `conflicting_ops` precomputation is O(n^2) bit_vectors of size num_ops:
  memory hog (n<=2000 guard). Store only upper triangle; or lazily compute.
- Hash maps: `max_sscp_node_pre_cache` keys copy the abstraction_ids vector
  into every NodeKey. Consider arena/interning of id vectors.
- `SSCPNode::children` vector<shared_ptr>: shared_ptr refcount churn during
  DAG construction (sort, splice). Consider raw pointers + arena ownership.
- `create_structured_scp_order`: recursive collect_nodes may be deep;
  fine, but the level-sort is O(n log n) — cheap. Skip.
- Heuristic evaluation: `values` recomputed fully per state; instructions
  walk vector<int> ids — cache-friendly already. Flatten ids into one
  contiguous buffer with offsets to cut allocations at construction.
- `compress_costs`: could hash the raw costs vector instead of packing it
  (packing exists only to deduplicate memory). Measure whether packing wins.

## Memory

- Lookup tables stored twice during construction (`lookup_tables` +
  transposed `compact_lookup_tables`) — peak doubles; transpose in place or
  free the source tables per abstraction while transposing.
- `conflicting_ops` full n×n matrix of bit_vectors (see above).
- `scf_cache` stores a full Costs vector per lookup node — dominate memory
  for many lookup nodes? Could store only non-default entries or recompute.
- elevators_mix probe: 561MB peak vs 47MB for sys2-only — cartesian path
  (explicit abstractions) or many abstractions dominate; profile where.
- `UnsolvabilityInfo.unsolvable_states` vector<bool> per abstraction even
  when never useful — allocate lazily.

## Clarity

- Remove `g_hacked_*` globals: thread options through constructors.
- `dump_tree`/`dump_node` unused debug helpers — delete.
- `time_connected_components`/`collect_time` option + timers: delete if not
  worth keeping.
- `max_lookup_table_entries` double + pow/floor logic: simplify to size_t.
- Duplicate copy_if branches in create_max_node (use_general_cp) — unify.
- `PackedInts`/hash functor placement in utils.h could move next to their
  only users (structured_scp files).
