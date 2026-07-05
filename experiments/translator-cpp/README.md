# C++ translator experiments

Downward Lab experiments evaluating the C++ translator port (`src/translate-cpp`)
against the Python translator (`src/translate`). All scripts share the boilerplate
in this directory instead of each carrying its own copy.

## Shared boilerplate

- `project.py` — Lab environment/report helpers (Tetralith/Basel/local).
- `pyproject.toml`, `uv.lock` — pinned dependencies; run scripts with `uv run`.
- `custom_parser.py` — one parser for all experiments: translator time/memory,
  output-shape attributes, and `sas_sha256` (the byte-equivalence key printed by
  `run-translate-cmp.sh`); unmatched patterns are simply absent per experiment.
- `run-translate.sh` — run one translator binary under `/usr/bin/time` (peak RSS).
- `run-translate-cmp.sh` — run one translator via `fast-downward.py --translator`
  (py or cpp) and print the output.sas sha256 for byte-equivalence checks.

## Experiments

- `2026-07-04-A-translator-revisions.py` — compare kept `translate-cpp` git
  revisions (runtime + peak memory), building each binary once into a SHA-keyed
  `data/translate-bin-cache`.
- `2026-07-04-B-translator-revisions-2.py` — follow-up revisions run; reuses A's
  binary cache.
- `2026-07-04-C-py-vs-cpp.py` — py vs cpp over downward-benchmarks, htg-domains,
  and Autoscale-Sat, with per-task byte-equivalence.
- `2026-07-05-D-ipc2023-learning.py` — py vs cpp over the IPC 2023 learning-track
  test set (10 domains x 90 tasks), with byte-equivalence.

## Running

```
uv run ./2026-07-04-C-py-vs-cpp.py            # list steps
uv run ./2026-07-04-C-py-vs-cpp.py build start parse fetch equivalence
```

On a Slurm cluster the environment is detected automatically and the steps are
submitted as a dependency chain.

## Artifacts

- `campaign-summary.html`, `py-vs-cpp-summary.html` — rendered result summaries.
