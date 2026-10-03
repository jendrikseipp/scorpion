# C++ translator optimization experiments

`bench.py` probes benchmark tasks and compares immutable standalone translator
binaries. It uses only the Python standard library and GNU time on Linux.
Benchmark suites contain `domain:problem.pddl` entries; the resolver follows
the driver's naming rules, including Airport's `pNN-domain.pddl` files.

The repository defaults to the harness's checkout. Benchmarks default to
`$DOWNWARD_BENCHMARKS` or a sibling `downward-benchmarks` checkout. Use
`--benchmarks` to choose another location, `--cpu` to choose an available CPU,
and `--time-binary` if GNU time is outside `PATH`. The default limits are 120
seconds and 6144 MiB per run.

## Protocol

Build baseline and candidate with the same compiler and CMake Release flags.
Save each executable before changing sources. Stop compilation and competing
benchmark workloads while measuring. Runs are serial and pinned to CPU 2 by
default, with wall time, user plus system CPU time, and peak RSS collected by
external GNU time.

Screen candidates on `screen-suite.json`, then confirm promising changes on
the larger tasks in `confirmation-suite.json`. One repetition is a balanced
four-run ABBA or BAAB block, giving two measurements per variant. The first
variant alternates across tasks and blocks. Repeat promising experiments to
distinguish stable improvements from noise.

Every run must finish successfully and produce the same SHA256 of
`output.sas` as the baseline, including across repetitions. The regression
matrix uses the maintained translator test's option-diverse tasks and all
output-changing option configurations. Review code clarity separately, paying
attention to ownership, redundant representations and control-flow complexity.

Keep a performance change when improvement is repeatable and material:
normally at least 5% across the suite, or 10% on its target workload, with no
material loss in the other performance measure or code clarity. Investigate
aggregate regressions above 3% or individual regressions above 10% before
deciding. A substantial clarity improvement also needs measurements showing
that runtime and memory remain acceptable. Record accepted and rejected ideas,
their patches, and the evidence; update the baseline only after acceptance.

## Commands

Run these from the repository root. `baseline-bin` and `candidate-bin` are
saved standalone `translate-cpp` executables; each output directory must be new
or empty.

```sh
python3 experiments/translator-cpp/autoresearch/bench.py probe \
  --baseline ./baseline-bin --domains freecell logistics98 tidybot-opt14-strips \
  --timeout 20 --out ./probe-results

python3 experiments/translator-cpp/autoresearch/bench.py ab \
  --baseline ./baseline-bin --candidate ./candidate-bin \
  --suite experiments/translator-cpp/autoresearch/screen-suite.json \
  --repetitions 3 --quiet --out ./screen-results

python3 experiments/translator-cpp/autoresearch/bench.py ab \
  --baseline ./baseline-bin --candidate ./candidate-bin \
  --suite experiments/translator-cpp/autoresearch/confirmation-suite.json \
  --repetitions 3 --quiet --out ./confirmation-results

python3 experiments/translator-cpp/autoresearch/bench.py validate \
  --baseline ./baseline-bin --candidate ./candidate-bin \
  --regression --quiet --out ./validation-results
```

Use `--tasks domain:problem.pddl` for selected tasks, or provide a JSON suite.
Task objects can specify `task`, `config` and an `options` string list. Extra
translator arguments can be appended with `--option=--flag` and
`--option=value`.

## Results and exclusions

Each result directory retains executable hashes and resolved tasks in
`metadata.json`, raw `runs.jsonl`, paired `pairs.jsonl`, per-run translator and
timing logs, every SAS output, and `summary.json`. Ratios are candidate divided
by baseline: below 1 improves. Aggregates give each task equal weight. The 95%
bootstrap intervals resample paired measurements within each task and describe
repeat-run noise for this fixed suite; they do not establish generality across
all planning domains.

Timeouts, resource failures, missing output, mismatched hashes, and logged
invariant-generation time cutoffs fail the suite. Failed tasks are excluded
from numeric aggregate ratios and remain explicit failures. They must never be
counted as successful comparisons, even when both variants fail. A/B runs
below GNU time's resolution also fail measurement; choose larger tasks.
`validate` still compares successful outputs on such small tasks.

The non-split organic-synthesis confirmation task can require substantial time
and memory. Use the C++ baseline as its reference; the Python translator is
known to exceed practical resources on this family. A limit failure is
reported and is not evidence for or against the candidate's speed.
