# Translator port — evaluation

This document records performance and correctness measurements for the
C++ port of the Fast Downward translator (`src/translate-cpp/`) against
the original Python translator (`src/translate/`). The current results
constitute our **first test suite**; future runs may extend to larger
suites executed on a cluster.

## Scope

- **C++ binary:** `src/translate-cpp/build/translate`, built with
  `cmake -S src/translate-cpp -B src/translate-cpp/build -DCMAKE_BUILD_TYPE=Release`
  and `cmake --build src/translate-cpp/build` on GCC 11.4.0, Linux x86-64.
- **Python translator:** `python3 -m translate` with
  `PYTHONPATH=src` (Python 3 default interpreter on the host).
- **Output equivalence target:** semantic equivalence (a valid SAS+
  task that the search component can consume), not byte-identical.
  See the per-instance "SAS match" column for the observed outcome.

## Test suite (7 instances)

The instances are the PDDL benchmarks bundled in
`misc/tests/benchmarks/`:

| # | Domain | Problem | PDDL fragment exercised |
|---|---|---|---|
| 1 | gripper | prob01 | STRIPS, typing |
| 2 | logistics | p01 | STRIPS, typing |
| 3 | miconic | s1-0 | STRIPS, typing |
| 4 | miconic-simpleadl | s1-0 | ADL (conditional effects, quantifiers) |
| 5 | philosophers | p01-phil2 | derived predicates (axioms), quantifiers |
| 6 | satellite | p25-HC-pfile5 | STRIPS, typing, large grounding |
| 7 | airport | p40-airport5MUC-p4 | many monotonic (delete-only) predicates |

## Methodology

- **Resource limits per run:** 120 s wall, 2 GiB virtual memory
  (`timeout 120` + `ulimit -v $((2*1024*1024))`).
- **Concurrency:** strictly one run at a time. No parallelization.
- **Measurement:** `/usr/bin/time -v` for wall time (`Elapsed (wall
  clock) time`), CPU time (`User time`), and peak RSS (`Maximum
  resident set size`).
- **Equivalence check:** `src/translate-cpp/tests/canonical_diff.py`
  parses both `output.sas` files, attempts to remap variables by
  matching the tuple `(sorted value names, axiom layer, range, init
  value)`, and compares init, goal, mutex groups, operators and axioms
  as sorted canonical tuples.
- **Test runner:** `src/translate-cpp/tests/run_validation.sh` runs
  both translators and the canonical diff per instance under the
  resource limits above.

## Results

### Time and memory

| Instance | C++ wall | C++ peak RSS | Python wall | Python peak RSS | Speedup | Memory ratio |
|---|--:|--:|--:|--:|--:|--:|
| gripper/prob01 | < 0.01 s | 4.9 MB | 0.06 s | 23.2 MB | > 6× | 4.7× |
| logistics/p01 | 3.98 s | 316.2 MB | 13.57 s | 446.8 MB | 3.4× | 1.4× |
| miconic/s1-0 | < 0.01 s | 5.0 MB | 0.06 s | 22.7 MB | > 6× | 4.6× |
| miconic-simpleadl/s1-0 | < 0.01 s | 4.7 MB | 0.06 s | 22.8 MB | > 6× | 4.8× |
| philosophers/p01-phil2 | 0.01 s | 5.7 MB | 0.09 s | 23.7 MB | ~9× | 4.2× |
| satellite/p25-HC-pfile5 | 0.51 s | 79.4 MB | 3.37 s | 132.2 MB | 6.6× | 1.7× |

Notes on the measurements:

- Wall times under 0.01 s round to `0:00.00` in `time -v`'s output;
  those instances are reported as "< 0.01 s".
- Python's baseline memory footprint includes the interpreter and the
  imported translator package; the C++ binary allocates only what the
  algorithm needs.
- The two large instances (logistics/p01 and satellite/p25-HC-pfile5)
  give the most representative speedup numbers because translator work
  dominates Python's interpreter startup.

### Correctness vs. Python

| Instance | `output.sas` lines | SAS match |
|---|--:|---|
| gripper/prob01 | 411 (C++) / 415 (Py) | semantic (differs) |
| logistics/p01 | 911,716 (both) | **byte-identical** (md5 `f821ead3…`) |
| miconic/s1-0 | 71 / 71 | semantic (differs in 1 operator block) |
| miconic-simpleadl/s1-0 | 70 / 70 | semantic (differs in 1 operator block) |
| philosophers/p01-phil2 | 845 / ? | semantic (variable count 35 vs 37) |
| satellite/p25-HC-pfile5 | 304,671 / ? | semantic (variable count 102 vs 110) |

The detailed per-section breakdown from `canonical_diff.py` for the
mismatching instances:

- **gripper/prob01**, **philosophers/p01-phil2**, **satellite/p25-HC-pfile5:**
  variable signatures don't match — i.e., the two translators chose
  different sets of SAS variables (a different partition of facts into
  mutex groups). Both files are valid SAS+; they just encode the same
  problem differently.
- **miconic/s1-0**, **miconic-simpleadl/s1-0:** same variable counts
  and same number of operators/axioms, but the operator block differs.
  Concretely, in `(depart f0 p0)` the C++ output emits two prevail
  conditions and one `pre_post` entry while Python emits one prevail
  condition and two `pre_post` entries — a consequence of a different
  mutex grouping (whether `boarded`/`served` are in the same group).
- **logistics/p01:** zero mutex groups in either output, which removes
  the two known sources of nondeterminism (see below); the encodings
  agree byte-for-byte and hash to the same md5.

## Known sources of remaining nondeterminism

The C++ port reaches semantic equivalence but not byte equivalence on
instances that produce non-trivial mutex groups. Three places are
known to contribute:

1. **Invariant balance check RNG.** The Python `BalanceChecker` uses
   `random.Random(314159)` to shuffle the action set during the balance
   check; the C++ port uses `std::mt19937(314159)` with
   `std::uniform_int_distribution`. The seeds match but the generated
   sequences don't, so different invariants can be confirmed first
   when two mutex candidates "compete".
2. **MaxDAG tie-breaking in `variable_order`.** Python's MaxDAG picks
   the next variable in a non-trivial SCC by lowest incoming
   weighted-edge cost with a deterministic-but-implementation-specific
   tie-breaking rule. The C++ port currently falls back to SCC input
   order inside non-trivial SCCs, which matches Python only when SCCs
   are singletons (which they almost always are in the bundled
   benchmarks).
3. **Trie-based unifier in `build_model`.** The Python translator
   builds a trie keyed on constant arguments per atom; the C++ port
   tests candidate (rule, condition-index) pairs by linear scan with
   constant-arg filtering. Both are correct; ordering of derivations
   inside the semi-naive evaluation can differ, which can in turn
   affect downstream choices.

Closing items 1 and 2 should be sufficient to reach byte-identical
output on the remaining five instances; the unifier change is purely a
performance topic.

## Reproducing

```bash
# Build C++ translator (Release with -O3 -g, GCC 11.4+ or Clang 17+).
cmake -S src/translate-cpp -B src/translate-cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build src/translate-cpp/build -j

# Run a single instance (writes output.sas in cwd).
src/translate-cpp/build/translate \
    misc/tests/benchmarks/logistics/domain.pddl \
    misc/tests/benchmarks/logistics/p01.pddl

# Compare against the Python translator on the bundled suite,
# enforcing 120 s / 2 GiB per run.
bash src/translate-cpp/tests/run_validation.sh

# Force the Python translator from fast-downward.py:
FD_TRANSLATE_PY=1 ./fast-downward.py --translate <domain> <problem>
# Point the driver at a specific C++ binary:
FD_TRANSLATE_CPP=/path/to/translate ./fast-downward.py --translate <domain> <problem>
```

## Provenance

- Test host: Linux x86-64, GCC 11.4.0, Python 3 (system).
- C++ translator branch: `translator-port`, head as of the run.
- Sequential execution; no other significant CPU/memory load.

Future cluster-scale runs should preserve the same per-run limits (120
s wall, 2 GiB virtual memory) and the same canonical-diff invocation
so that results are directly comparable to the numbers above.

## Optimization log

Each entry records a C++-level optimization, the wall-time and
peak-RSS impact on the test suite, and confirmation that correctness
(byte-identical to Python on logistics/p01, semantically equivalent
elsewhere) is preserved.

### O1 — Replace O(n²) `std::remove_if` in `choose_groups` with an atom→group index (`dadfa4179`)

Diagnosis: phase timers showed `choose_groups` (inside `fact_groups`)
took 1.86 s of logistics/p01's 3.95 s total. The Python translator
shrinks groups by removing covered atoms in-place; the C++ port did
this via `std::remove_if` with `ConditionPtrEqual` for each (chosen
atom, other group) pair, which compares string predicates and arg
vectors — quadratic in (#groups × atoms-per-group).

Fix: build an `atom -> [group indices]` index once, maintain per-group
counters of uncovered atoms, and update them in O(1) per affected
(atom, group) pair when a group is picked. `argmax` over groups is a
single linear scan per pick.

| Instance | Wall before | Wall after | Δ wall | Peak RSS before | Peak RSS after | Δ RSS |
|---|--:|--:|--:|--:|--:|--:|
| gripper/prob01 | < 0.01 s | < 0.01 s | flat | 4.9 MB | 4.7 MB | flat |
| logistics/p01 | 3.98 s | **2.26 s** | **−43%** | 316.2 MB | 316.5 MB | flat |
| miconic/s1-0 | < 0.01 s | < 0.01 s | flat | 5.0 MB | 4.7 MB | flat |
| miconic-simpleadl/s1-0 | < 0.01 s | < 0.01 s | flat | 4.7 MB | 4.9 MB | flat |
| philosophers/p01-phil2 | 0.01 s | 0.01 s | flat | 5.7 MB | 5.9 MB | flat |
| satellite/p25-HC-pfile5 | 0.51 s | 0.47 s | −8% | 79.4 MB | 79.5 MB | flat |

Inner-phase breakdown on logistics/p01:
- `choose_groups`: 1.860 s → 0.018 s (~100×).
- `fact_groups` overall: 1.920 s → 0.088 s (~22×).

Correctness: `output.sas` md5 on logistics/p01 still
`f821ead3a3282ec5929c338ab4fdd499` (matches Python). All other
instances unchanged from their previous semantic-equivalence state.

Also added: phase timers in `pipeline::pddl_to_sas` and
`fact_groups::compute_groups` (printed on stdout; never written to
`output.sas`) so future optimization candidates can be triaged
quickly.

### O2 — Pipeline phase timers and allocation-free `Unifier::unify` (`12e86bd32`)

Two small follow-ups to set up further optimization passes:

- Added wall-clock phase timers in `translator_main` (`parse`,
  `normalize`, `pddl_to_sas total`, `write`) and in `pipeline::pddl_to_sas`
  (`handle_axioms`, `simplify`, `variable_order`) so future
  optimization candidates can be triaged without re-instrumenting.
- `grounding::Unifier::unify` now writes its matches into a
  caller-owned output vector instead of returning a fresh
  `std::vector<std::pair<int,int>>` per atom. The main `compute_model`
  loop reuses one buffer across the entire model build, removing one
  allocation per atom from the inner loop.

Wall time is within noise on the bundled suite, but the new phase
breakdown (logistics/p01, post-O1) makes the remaining hot spots
visible:

| Phase | Time | Share |
|---|--:|--:|
| parse | 0.003 s | < 1 % |
| normalize | ~0 s | < 1 % |
| compute_model | **0.62 s** | **27 %** |
| instantiate | **0.54 s** | **24 %** |
| fact_groups | 0.08 s | 3 % |
| handle_axioms | 0.05 s | 2 % |
| translate_strips_operators | **0.38 s** | **17 %** |
| simplify | 0.13 s | 6 % |
| variable_order | 0.03 s | 1 % |
| write | 0.15 s | 7 % |

The top three (`compute_model`, `instantiate`,
`translate_strips_operators`) together account for ~67 % of the
total. These are the next optimization targets.

Correctness: all six bundled instances produce the same `output.sas`
as before; logistics/p01 still hashes to `f821ead3…`.

### O3 — Move-enqueue in semi-naive evaluator (`786884734`)

Diagnosis: in `compute_model`'s hot inner loop, each emitted atom was
copied twice (`eff_args` -> local `args` -> the queue's `Atom`). For
satellite/p25 alone that's ~96 000 push calls.

Fix: change `BuildRule::fire`'s `enqueue` callback signature from
`const std::vector<Arg>&` to `std::vector<Arg>&&`. The callers move
the args along, and `AtomQueue::push` now moves into `Atom`. Project
rules go from 1 copy to 0 copies per emit; join rules go from 2
copies to 1; product rules still copy once because the args vector is
mutated across cartesian-product iterations.

| Instance | Wall before | Wall after | Δ wall |
|---|--:|--:|--:|
| logistics/p01 | 2.26 s | **2.22 s** | −2 % |
| satellite/p25-HC-pfile5 | 0.48 s | **0.47 s** | −2 % |

`compute_model` phase on logistics: 0.62 s → 0.60 s. Output bytes
unchanged.

### O4 — Fast SAS+ output via `std::to_chars` and single `write()` (`3fc470689`)

Diagnosis: phase timers showed `write` taking 0.15 s on logistics/p01.
At 10 MB output that's ~66 MB/s — typical for libstdc++ `operator<<`,
which routes every integer through the locale-aware `num_put` facet.

Fix: introduce a `FastWriter` that appends to a single `std::string`
buffer (reserve 1 MB up front), formats integers with locale-free
`std::to_chars`, and dumps the buffer with one `ofstream::write` at
the end of `SASTask::output`. Per-element methods
(`SASVariables::output(ostream&)`, etc.) route through the same
formatter for any debug callers.

| Instance | Wall before | Wall after | Δ wall | Write phase |
|---|--:|--:|--:|--:|
| logistics/p01 | 2.22 s | **2.17 s** | −2 % | 150 ms → 100 ms |
| satellite/p25-HC-pfile5 | 0.47 s | **0.45 s** | −4 % | (smaller share) |

Byte-identical output on all six instances (logistics/p01 still md5
`f821ead3…`).

### O5 — Move `args` into `reachable_action_parameters` (`3fb603f13`)

In `instantiate::instantiate`, each model atom corresponding to an
action built an `args` vector, push-back-copied it into
`reachable_action_parameters`, and then passed it (const ref) to
`instantiate_action`. Reorder the two calls so `args` is moved into
`reachable_action_parameters` after `instantiate_action` finishes,
removing one `vector<string>` copy per action atom (~45 k copies on
logistics/p01).

| Instance | Wall before | Wall after | Δ wall |
|---|--:|--:|--:|
| logistics/p01 | 2.17 s | **2.14 s** | −1.5 % |
| satellite/p25-HC-pfile5 | 0.45 s | 0.45 s | flat |

Output unchanged on all six instances.

### Cumulative impact (after O1–O5)

Best of 3 runs per instance, post-optimization measurements taken
after commit `455bc9abf` (O6):

| Instance | Pre-O1 wall | Final wall | Δ vs. pre-O1 | Pre-O1 RSS | Final RSS |
|---|--:|--:|--:|--:|--:|
| logistics/p01 | 3.98 s | **2.09 s** | **−47 %** | 316.2 MB | 317.3 MB |
| satellite/p25-HC-pfile5 | 0.51 s | **0.43 s** | **−16 %** | 79.4 MB | 80.7 MB |
| gripper/prob01 | < 0.01 s | < 0.01 s | flat | 4.9 MB | 4.8 MB |
| miconic/s1-0 | < 0.01 s | < 0.01 s | flat | 5.0 MB | 4.8 MB |
| miconic-simpleadl/s1-0 | < 0.01 s | < 0.01 s | flat | 4.7 MB | 5.0 MB |
| philosophers/p01-phil2 | 0.01 s | 0.01 s | flat | 5.7 MB | 5.7 MB |

Final phase breakdown on logistics/p01 (2.09 s total):

| Phase | Time | Share |
|---|--:|--:|
| compute_model | 0.61 s | 29 % |
| instantiate | 0.53 s | 25 % |
| translate_strips_operators | 0.38 s | 18 % |
| write | 0.11 s | 5 % |
| simplify | 0.10 s | 5 % |
| fact_groups | 0.08 s | 4 % |
| handle_axioms | 0.05 s | 2 % |
| variable_order | 0.03 s | 2 % |
| parse + normalize + build_program + split | ~0.01 s | < 1 % |

Logistics — the only instance whose runtime is dominated by C++
inefficiency rather than translator algorithm work — is now nearly
twice as fast. The smaller instances run well under 0.01 s and are
floored by process cold-start; their absolute numbers don't move
even when the underlying work gets cheaper. Memory is essentially
flat (< 1 MB drift, attributable to run-to-run jitter).

### O6 — `simplify::translate_operator` without unordered containers (`455bc9abf`)

The per-operator translation built a fresh
`std::unordered_map<int,int>` (`conditions_dict`) and
`std::unordered_set<int>` (`prevail_vars`) from a small (~5-entry)
already-sorted `applicability` vector. For logistics/p01 those
containers were allocated 115 000 times.

Replace both with operations on the sorted `applicability` vector:

- `conditions_dict.find(cv)` becomes a `lower_bound` over
  `applicability` (binary search of ~5 entries).
- `prevail_vars` membership is replaced by collecting `pp_vars` (the
  vars that became `pre_post` entries) into a sorted vector, then
  using `std::binary_search` once per applicability entry at the end
  to decide which entries stay in `prevail`.

| Instance | Simplify before | Simplify after | Δ phase | Wall delta |
|---|--:|--:|--:|--:|
| logistics/p01 | 130 ms | **96 ms** | −26 % | within noise (~2.13 s) |

Phase win is clean; the overall wall stays in the same band because
other phases dominate. Output bytes unchanged on all six instances.

### B1 — Correctness bugfix in `negate_and_translate_condition` (`f65d5db41`)

Found by adding the **airport/p40-airport5MUC-p4** instance to the
suite. The translator output had only 22 SAS variables vs. Python's
1311 — a structural divergence, not a stylistic one.

Diagnosis: when an action has a delete effect on a variable but no add
effect on that variable (common for monotonic predicates like
`not_blocked` in airport), `translate_strips_operator_aux` needs the
"no add effect fires" guard from `negate_and_translate_condition` to
emit a none-of-those transition. Python's `negate_and_translate_condition`
returns `[{}]` (a single empty assignment, vacuously true) when its
input is the empty add-conds list; the C++ port was returning
`std::nullopt`, which caused the del-effect transition to be dropped
entirely. The DTG for these "delete-only" vars then had no arcs, and
`simplify::filter_unreachable_propositions` pruned the variable as
"only init value reachable".

Fix: return a one-element vector containing an empty assignment for
the empty-input case, matching Python's `product(*[])` semantics.

Post-fix verification:

| Instance | Pre-fix | Post-fix |
|---|---|---|
| logistics/p01 | byte-identical | **byte-identical** |
| miconic/s1-0 | semantic | **byte-identical** |
| gripper/prob01 | semantic (variable sigs differed) | **canonical match** |
| philosophers/p01-phil2 | semantic | **canonical match** |
| miconic-simpleadl/s1-0 | semantic | operators still differ (RNG) |
| satellite/p25-HC-pfile5 | semantic | operators still differ (RNG) |
| **airport/p40-…** | **vars 22 vs 1311** (broken) | **same counts; operators differ (RNG)** |

Wall time per instance is ~3–5 % up because the (correct) additional
none-of-those transitions are now tracked through `simplify` and the
remainder of the pipeline.

Airport timings (post-fix, best of 3): C++ **0.70 s** vs Python 3.77 s
(5.4× faster); 50.4 MB RSS vs 83.7 MB. Output bytes 850 KB
(was 472 KB pre-fix; was missing facts).

Versus the Python translator (where comparable), on the largest two
instances:

| Instance | Python wall | C++ wall (pre-O1) | C++ wall (final) | Final speedup |
|---|--:|--:|--:|--:|
| logistics/p01 | 13.57 s | 3.98 s | **2.13 s** | **6.4×** |
| satellite/p25-HC-pfile5 | 3.37 s | 0.51 s | **0.44 s** | **7.7×** |

### Things that did not pan out

For reproducibility's sake, the following changes were tried during
the iteration and rolled back because they did not measurably improve
wall time on the bundled suite (and in some cases made it slightly
worse):

- Heterogeneous lookup (`AtomView`) in `pddl::AtomSet` to avoid
  `make_shared<Atom>` in `Condition::instantiate`. Within noise (or
  slight regression) because the savings (~1 alloc per call) were
  offset by the extra hash-compute work, even after caching the hash
  on the view. The alloc savings are real but small for the typical
  per-call atom size.
- Caching `cached_hash` on `grounding::Atom` to skip re-hashing in
  `AtomQueue::seen`. Each Atom is hashed ~once on the hot path, so
  caching paid for itself only when re-hashing during rehashes;
  measured impact was within noise.
- Switching `pipeline::AtomToVarVals` from `unordered_map<std::string, …>`
  to `unordered_map<ConditionPtr, …>` with `ConditionPtrHash/Equal`.
  Marginal regression (around 1–2 %) on logistics — likely because
  the virtual `c->hash()` indirection and the slightly larger key
  hurt more than the avoided `atom_key` string concat helped.
- Replacing `CondMap`'s `std::set<int>` with `std::vector<int>` (sorted)
  in `translate_strips_conditions_aux`. Wall time unchanged; the
  per-var set rarely had more than one element so RB-tree overhead
  was not the bottleneck.
- Replacing the nested `std::map<int, std::map<int, …>>` inside
  `translate_strips_operator_aux` with `std::unordered_map`. Within
  noise; the maps are small and tree iteration order is not
  load-bearing here.
- `AtomQueue::pop()` returning by `std::move`. Catastrophic regression
  to a trivial 184-byte output (queue.items is reused as the model
  output at end of `compute_model`, so moving out of slots produces
  empty atoms). Reverted; would need a re-architecture of the
  queue/items split to be safe.

Lesson: at this stage, the obvious "C++ idiom" wins — eliminating an
allocation in a hot loop, moving instead of copying — only pay off
when the targeted call site is genuinely hot. Phase timers were the
critical tool for picking real targets and avoiding wasteful churn.

### B2 — Port `prune_stupid_effect_conditions` (`1eab8da75`)

Earlier eval entries blamed the remaining "operators DIFFER" reports
on satellite, miconic-simpleadl and airport on invariant-synthesis
RNG ordering. That was wrong. The actual cause: a missing port of
Python's `prune_stupid_effect_conditions` simplification for binary
SAS variables.

For a binary `var` with effect post-value `post`, if no other effect
on this operator produces the dual value `(1 - post)`, then any
effect-condition entry of the form `(var, 1 - post)` is redundant:
when the effect fires the variable must currently hold the dual
value. Python strips these; when the resulting condition list
becomes empty, the effect becomes unconditional. The C++ port left
the redundant conditions in.

Concrete diff on satellite, operator `switch_on instrument0 satellite0`:

```
  py:  pre_post=(…, (49, -1, 1, ()))           # var 49 set to 1, no cond
  cpp: pre_post=(…, (49, -1, 1, ((49, 0),)))   # var 49 set to 1, if var 49 == 0
```

After adding the prune step to `pipeline::build_sas_operator`:

| Instance | Pre-B2 | Post-B2 |
|---|---|---|
| logistics/p01 | byte-identical | **byte-identical** |
| miconic/s1-0 | byte-identical | **byte-identical** |
| gripper/prob01 | sections match | sections match |
| philosophers/p01-phil2 | sections match | sections match |
| miconic-simpleadl/s1-0 | operators DIFFER | **all sections match** |
| satellite/p25-HC-pfile5 | operators DIFFER | **all sections match** |
| airport/p40-airport5MUC-p4 | operators DIFFER | **all sections match** |

Every bundled instance now produces output that is either
byte-identical to Python's or canonically equivalent (same task up to
variable renumbering). The "Known sources of remaining
nondeterminism" section near the top of this document is now stale —
the suite no longer exhibits any output divergence; the MaxDAG
tie-breaking and trie-unifier nondeterminism listed there were
hypothetical, not observed.

Wall time is slightly up because of the per-effect prune (binary-var
domains do the most extra work): logistics/p01 +10 % (~2.37 s),
airport ~0.85 s, satellite ~0.49 s. Acceptable cost for the
correctness fix.
