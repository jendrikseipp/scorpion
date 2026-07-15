# Perfect SCP autoresearch log

This directory records the autonomous optimization loop that integrated and
then sped up the perfect saturated cost partitioning heuristic (the
"structured SCP order generator", `src/search/cost_saturation/structured_scp_*`),
following Höft, Speck & Seipp, *Sensitivity Analysis for SCP Heuristics*,
KR 2025.

Contents:

- `autoresearch.md` — the running log: objective, metrics, the probe suites,
  what was tried (kept and rejected, with reasons), the paper/artifact
  analysis, and the grid-experiment results.
- `autoresearch.jsonl` — append-only ledger of every experiment (median
  metric, raw samples, decision).
- `autoresearch.ideas.md` — ideas backlog and profiling notes.
- `autoresearch.sh` — interleaved A/B benchmark (peak memory during DAG
  generation on blowup tasks); prints `METRIC` lines.
- `time-suite.sh` — the earlier construction-time A/B suite, kept as a guard.
- `autoresearch.checks.sh` — correctness checks (optimal plan costs, initial
  heuristic values).
- `probe.sh`, `translate_probe.sh` — helpers to translate and probe tasks.

The scripts expect the reference binaries and translated probe tasks under
`.autoresearch/` at the repository root (git-ignored, machine-local); they
are not part of this record.
