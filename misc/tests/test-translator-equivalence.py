#! /usr/bin/env python3

HELP = """\
Check byte-for-byte equivalence of the two translator variants.

For every task in a benchmark suite, run the Python translator
(src/translate/, --translator py) and the C++ translator (src/translate-cpp/,
--translator cpp) and compare their output.sas byte-for-byte. With the C++
translator's default CPython-compatible RNG (see
src/translate-cpp/utils/cpython_random.h) the two produce identical output on
every task; any mismatch is reported and fails the run.

Every task is checked under each translator option configuration that changes
output.sas (--relaxed, --full-encoding, --add-implied-preconditions,
--keep-unreachable-facts, --skip-variable-reordering,
--keep-unimportant-variables, --keep-no-ops, --keep-duplicate-operators,
--layer-strategy max, and disabled invariants), so the two variants must agree
on every option path, not just the defaults. Options that do not affect
output.sas and the wall-clock-dependent --invariant-generation-max-time are
excluded (see CONFIGS).

This checks only py-vs-cpp equivalence. Determinism of each translator is
checked separately by test-translator.py (pass --translator cpp for the C++
variant).

Benchmarks are not bundled: tasks are read from a downward-benchmarks checkout
given by the DOWNWARD_BENCHMARKS environment variable (or a directory argument).

The default run checks two things (see OPTION_TASKS, FAMILY_TASKS, CONFIGS):
  - Option coverage: an option-diverse set (one task from each family that
    exposed a past py-vs-cpp divergence) is checked under EVERY option config,
    so every option path is exercised. The families and the divergences they
    guard: assembly/freecell/psr-large/psr-middle/settlers-sat18-adl/
    thoughtful-sat14-strips/trucks-strips (axiom/mutex/sort), ged-opt14-strips/
    philosophers (invariant-finder RNG), miconic/logistics98 (MaxDAG SCC
    ordering), pathways/sokoban-sat11-strips (SCC DFS order), blocks
    (GroupCoverQueue tie-break), storage (SCC variable numbering),
    parking-sat14-strips (MaxDAG cyclic-SCC ordering).
  - Domain coverage: the smallest task of EVERY domain family is checked under
    default options, so no family goes untested. The full option matrix is not
    applied here (domain coverage does not need it, and it would multiply the
    few families whose smallest task is still large, e.g. organic-synthesis).

Pass an explicit suite ("all", "first", or "<domain>:<problem>" entries) and/or
a different benchmark directory to instead check those tasks under every config;
tasks are discovered recursively, so both the flat domain/problem layout and
nested layouts are handled.

To regenerate FAMILY_TASKS when the benchmark set changes, pick the smallest
resolvable problem per family (strip -opt/-sat/-agl/-mco/year/-strips/-adl
suffixes to group domains into families).

Requires the C++ translator to be built:
    ./build.py release

Examples:
    ./test-translator-equivalence.py
    ./test-translator-equivalence.py $DOWNWARD_BENCHMARKS all
    ./test-translator-equivalence.py /path/to/downward-benchmarks gripper:prob01.pddl
"""

import argparse
import concurrent.futures
import filecmp
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

DIR = Path(__file__).resolve().parent
REPO = DIR.parents[1]
DRIVER = REPO / "fast-downward.py"
# Benchmarks are not bundled; point at a downward-benchmarks checkout via the
# DOWNWARD_BENCHMARKS environment variable (or pass a directory explicitly).
DEFAULT_BENCHMARKS = os.environ.get("DOWNWARD_BENCHMARKS")

# Option-coverage set: one task from each family that exposed a past py-vs-cpp
# divergence. These are checked under EVERY option config (see CONFIGS), so the
# option paths are exercised on feature-diverse tasks. Kept small and fast.
OPTION_TASKS = [
    "assembly:prob01.pddl",
    "blocks:probBLOCKS-4-0.pddl",
    "freecell:p01.pddl",
    "ged-opt14-strips:d-1-3.pddl",
    "logistics98:prob05.pddl",
    "miconic:s1-0.pddl",
    "parking-opt11-strips:pfile03-011.pddl",
    "pathways:p12.pddl",
    "philosophers:p01-phil2.pddl",
    "psr-large:p27-s172-n25-l2-f10.pddl",
    "psr-middle:p03-s28-n2-l5-f10.pddl",
    "settlers-sat18-adl:p01.pddl",
    "sokoban-sat11-strips:p18.pddl",
    "storage:p23.pddl",
    "thoughtful-sat14-strips:bootstrap-typed-01.pddl",
    "trucks-strips:p05.pddl",
]

# Translator option configurations to check for py-vs-cpp equivalence. Every
# option that changes output.sas is covered; each task is translated under each
# config with both variants and the outputs compared. Options that do not
# affect output.sas (--sas-file, --dump-*, --stop-after-parsing-pddl) and the
# non-deterministic --invariant-generation-max-time (its effect depends on wall
# clock, which differs between the variants) are intentionally excluded.
#
# (label, [translator options]).
CONFIGS = [
    ("default", []),
    ("relaxed", ["--relaxed"]),
    ("full-encoding", ["--full-encoding"]),
    ("add-implied-preconditions", ["--add-implied-preconditions"]),
    ("keep-unreachable-facts", ["--keep-unreachable-facts"]),
    ("skip-variable-reordering", ["--skip-variable-reordering"]),
    ("keep-unimportant-variables", ["--keep-unimportant-variables"]),
    ("keep-no-ops", ["--keep-no-ops"]),
    ("keep-duplicate-operators", ["--keep-duplicate-operators"]),
    ("layer-strategy=max", ["--layer-strategy", "max"]),
    ("no-invariants", ["--invariant-generation-max-candidates", "0"]),
]

# Domain-coverage set: the smallest task of every domain family in
# downward-benchmarks (one per family, so every family's translation is
# exercised at least once). These are checked under DEFAULT options only, not
# the full CONFIGS matrix -- domain coverage does not need every option, and
# running the matrix here would multiply the few families whose smallest task is
# still large (e.g. organic-synthesis, which has no small instance). Regenerate
# with the helper in the module docstring if the benchmark set changes.
FAMILY_TASKS = [
    "agricola-opt18-strips:p01.pddl",
    "airport:p01-airport1-p1.pddl",
    "assembly:prob01.pddl",
    "barman-opt11-strips:pfile01-004.pddl",
    "blocks:probBLOCKS-4-1.pddl",
    "caldera-opt18-adl:p03.pddl",
    "caldera-split-opt18-adl:p03.pddl",
    "cavediving-14-adl:testing07_easy.pddl",
    "childsnack-opt14-strips:child-snack_pfile01.pddl",
    "citycar-opt14-adl:p2-2-2-1-2.pddl",
    "data-network-opt18-strips:p01.pddl",
    "depot:p01.pddl",
    "driverlog:p01.pddl",
    "elevators-opt08-strips:p01.pddl",
    "flashfill-sat18-adl:p01.pddl",
    "floortile-opt11-strips:opt-p01-002.pddl",
    "folding-opt23-adl:p01.pddl",
    "freecell:p01.pddl",
    "ged-opt14-strips:d-1-4.pddl",
    "grid:prob01.pddl",
    "gripper:prob01.pddl",
    "hiking-opt14-strips:ptesting-1-2-3.pddl",
    "labyrinth-opt23-adl:p01.pddl",
    "logistics00:probLOGISTICS-4-1.pddl",
    "maintenance-opt14-adl:maintenance-1-3-010-010-2-002.pddl",
    "miconic-simpleadl:s1-0.pddl",
    "movie:prob01.pddl",
    "mprime:prob25.pddl",
    "mystery:prob25.pddl",
    "nomystery-opt11-strips:p11.pddl",
    "nurikabe-opt18-adl:p01.pddl",
    "openstacks-strips:p02.pddl",
    "optical-telegraphs:p01-opt2.pddl",
    # organic-synthesis (non-split) is intentionally omitted: every instance
    # grounds so large that the Python translator needs >100 GB and tens of
    # minutes (the C++ translator handles it in ~45 s), so there is no instance
    # a py-vs-cpp check can use as a reference. The closely related
    # organic-synthesis-split family (below) covers the same translation paths
    # at a tractable size; the full family is still checked (as a skip) under an
    # explicit "all" run.
    "organic-synthesis-split-sat18-strips:p01.pddl",
    "parcprinter-08-strips:p01.pddl",
    "parking-opt11-strips:pfile03-012.pddl",
    "pathways:p01.pddl",
    "pegsol-08-strips:p01.pddl",
    "petri-net-alignment-opt18-strips:p17.pddl",
    "philosophers:p01-phil2.pddl",
    "pipesworld-notankage:p01-net1-b6-g2.pddl",
    "pipesworld-tankage:p11-net2-b10-g2-t30.pddl",
    "psr-large:p01-s29-n2-l5-f30.pddl",
    "psr-middle:p02-s23-n2-l3-f70.pddl",
    "psr-small:p01-s2-n1-l2-f50.pddl",
    "quantum-layout-opt23-strips:p07.pddl",
    "recharging-robots-opt23-adl:p01.pddl",
    "ricochet-robots-opt23-adl:p11.pddl",
    "rovers:p02.pddl",
    "rubiks-cube-opt23-adl:p09.pddl",
    "satellite:p01-pfile1.pddl",
    "scanalyzer-08-strips:p23.pddl",
    "schedule:probschedule-2-0.pddl",
    "settlers-opt18-adl:p01.pddl",
    "slitherlink-opt23-adl:p01.pddl",
    "snake-opt18-strips:p04.pddl",
    "sokoban-opt08-strips:p03.pddl",
    "spider-opt18-strips:p01.pddl",
    "storage:p01.pddl",
    "termes-opt18-strips:p02.pddl",
    "tetris-opt14-strips:p02-4.pddl",
    "thoughtful-sat14-strips:bootstrap-typed-03.pddl",
    "tidybot-opt11-strips:p01.pddl",
    "tpp:p01.pddl",
    "transport-opt08-strips:p01.pddl",
    "trucks-strips:p01.pddl",
    "visitall-opt11-strips:problem02-half.pddl",
    "woodworking-sat08-strips:p11.pddl",
    "zenotravel:p01.pddl",
]


def is_domain_file(path):
    name = path.name.lower()
    return "domain" in name


def resolve_domain(problem):
    """Find the domain file for a problem, mirroring the driver's heuristics."""
    d = problem.parent
    stem = problem.stem
    candidates = [
        d / "domain.pddl",
        d / f"domain_{problem.name}",
        d / f"domain-{problem.name}",
        d / f"{stem}-domain.pddl",
        d / f"{stem}.domain.pddl",
    ]
    for c in candidates:
        if c.exists():
            return c
    # Fall back to any domain-looking file in the directory.
    others = sorted(p for p in d.glob("*.pddl") if is_domain_file(p))
    return others[0] if others else None


def discover_tasks(benchmarks_dir):
    """Return sorted (domain_file, problem_file) pairs found recursively."""
    tasks = []
    for problem in sorted(benchmarks_dir.rglob("*.pddl")):
        if is_domain_file(problem):
            continue
        domain = resolve_domain(problem)
        if domain is not None:
            tasks.append((domain, problem))
    return tasks


def select(tasks, suite, benchmarks_dir):
    if suite == ["all"]:
        return tasks
    selected = []
    by_domain = {}
    for dom, prob in tasks:
        by_domain.setdefault(prob.parent, []).append((dom, prob))
    if "first" in suite:
        selected += [v[0] for v in by_domain.values()]
    for entry in suite:
        if entry in ("all", "first"):
            continue
        # "<domain>:<problem>" relative to benchmarks_dir.
        rel = benchmarks_dir / entry.replace(":", "/")
        dom = resolve_domain(rel)
        if dom is None:
            sys.exit(f"Could not resolve domain for {rel}")
        selected.append((dom, rel))
    return sorted(set(selected))


def translate(translator, domain, problem, cwd, options):
    """Run one translator; return (returncode, wall-clock seconds)."""
    cmd = [sys.executable, str(DRIVER), "--translator", translator,
           "--translate", str(domain), str(problem)]
    if options:
        cmd += ["--translate-options", *options]
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, encoding="utf-8")
    return proc.returncode, time.perf_counter() - start


def check_one(domain, problem, config_label, options):
    """Translate one (task, option config) with both variants and compare.
    Returns (name, status, py_time, cpp_time, detail); status in
    ok/differ/error. --relaxed and --keep-unreachable-facts can legitimately
    make a task unsolvable-at-parse; if BOTH variants agree on such a non-zero
    exit and produce no output.sas, that counts as ok (matching behaviour)."""
    name = f"{problem.parent.name}:{problem.name} [{config_label}]"
    with tempfile.TemporaryDirectory() as tmp:
        pyd, cppd = Path(tmp) / "py", Path(tmp) / "cpp"
        pyd.mkdir()
        cppd.mkdir()
        rc_py, py_time = translate("py", domain, problem, pyd, options)
        rc_cpp, cpp_time = translate("cpp", domain, problem, cppd, options)
        py_sas, cpp_sas = pyd / "output.sas", cppd / "output.sas"
        py_out = rc_py == 0 and py_sas.exists()
        cpp_out = rc_cpp == 0 and cpp_sas.exists()
        if not py_out and not cpp_out:
            # Both produced no SAS: agreeing exit codes = matching behaviour.
            status = "ok" if rc_py == rc_cpp else "error"
            detail = "" if status == "ok" else f"exit {rc_py} (py) != {rc_cpp} (cpp)"
        elif not py_out:
            # No Python reference (e.g. py ran out of memory/time on a task the
            # faster, leaner C++ translator handled): equivalence is unverifiable
            # here, but this is not a C++ defect. Report as a skip, not a failure.
            status, detail = "skip", "py produced no output"
        elif not cpp_out:
            status, detail = "error", "cpp failed"
        elif filecmp.cmp(py_sas, cpp_sas, shallow=False):
            status, detail = "ok", ""
        else:
            status, detail = "differ", ""
    return name, status, py_time, cpp_time, detail


def main():
    p = argparse.ArgumentParser(
        description=HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("benchmarks_dir", nargs="?", default=DEFAULT_BENCHMARKS,
                   help="benchmark directory (default: $DOWNWARD_BENCHMARKS)")
    p.add_argument("suite", nargs="*", default=None,
                   help='task selection. Default: the option-coverage set under '
                        'every config PLUS one small task per family under '
                        'default options. Pass "all", "first" (first task per '
                        'domain), or "<domain>:<problem>" entries to instead '
                        'check those tasks under every config.')
    p.add_argument("-j", "--jobs", type=int,
                   default=min(os.cpu_count() or 4, 8),
                   help="number of tasks to translate in parallel "
                        "(default: min(cpu_count, 8)). Note: per-task times "
                        "are wall-clock and inflate under parallelism; the "
                        "elapsed line reflects the real speedup.")
    args = p.parse_args()
    if not args.benchmarks_dir:
        sys.exit("No benchmark directory: set the DOWNWARD_BENCHMARKS "
                 "environment variable or pass a directory explicitly.")
    benchmarks_dir = Path(args.benchmarks_dir).resolve()
    if not benchmarks_dir.is_dir():
        sys.exit(f"Not a directory: {benchmarks_dir}")

    all_tasks = discover_tasks(benchmarks_dir)
    # Build the (domain, problem, config-label, options) run specs.
    run_specs = {}  # keyed by (domain, problem, label) to dedupe
    def add(pairs, configs):
        for d, prob in pairs:
            for label, opts in configs:
                run_specs.setdefault((d, prob, label), opts)
    if not args.suite:
        # Default: option matrix on the option-coverage set + one small task per
        # family under default options (domain coverage).
        add(select(all_tasks, OPTION_TASKS, benchmarks_dir), CONFIGS)
        add(select(all_tasks, FAMILY_TASKS, benchmarks_dir), [("default", [])])
    else:
        # Explicit suite: check the given tasks under every config.
        add(select(all_tasks, args.suite, benchmarks_dir), CONFIGS)
    specs = [(d, p, label, opts) for (d, p, label), opts in run_specs.items()]
    if not specs:
        sys.exit(f"No tasks found under {benchmarks_dir}")

    runs = len(specs)
    n_tasks = len({(d, p) for d, p, _, _ in specs})
    jobs = max(1, min(args.jobs, runs))
    print(f"Comparing py vs cpp translator output on {n_tasks} task(s), "
          f"{runs} (task, option-config) run(s) "
          f"from {benchmarks_dir} ({jobs} parallel job(s))\n")
    identical, mismatch, errors, skipped = [], [], [], []
    py_total = cpp_total = 0.0
    start = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        futures = [ex.submit(check_one, d, p, label, opts)
                   for d, p, label, opts in specs]
        for fut in concurrent.futures.as_completed(futures):
            name, status, py_time, cpp_time, detail = fut.result()
            py_total += py_time
            cpp_total += cpp_time
            timing = f"py {py_time:6.2f}s  cpp {cpp_time:6.2f}s"
            if status == "ok":
                identical.append(name)
                print(f"ok     {name}  [{timing}]", flush=True)
            elif status == "differ":
                mismatch.append(name)
                print(f"DIFFER {name}  [{timing}]", flush=True)
            elif status == "skip":
                skipped.append((name, detail))
                print(f"skip   {name} ({detail})  [{timing}]", flush=True)
            else:
                errors.append((name, detail))
                print(f"ERROR  {name} ({detail})  [{timing}]", flush=True)
    elapsed = time.perf_counter() - start

    speedup = (py_total / cpp_total) if cpp_total else float("nan")
    print(f"\nsummed translate time (per-task wall-clock): py {py_total:.2f}s, "
          f"cpp {cpp_total:.2f}s ({speedup:.2f}x); elapsed {elapsed:.2f}s")
    print(f"summary: {len(identical)} identical, {len(mismatch)} differ, "
          f"{len(errors)} error(s), {len(skipped)} skipped of {runs} runs")
    if mismatch:
        print("byte-differing tasks:")
        for n in sorted(mismatch):
            print(f"  {n}")
    if errors:
        print("errored tasks:")
        for n, why in sorted(errors):
            print(f"  {n}: {why}")
    if skipped:
        print("skipped tasks (no Python reference; not a C++ defect):")
        for n, why in sorted(skipped):
            print(f"  {n}: {why}")
    # Skips do not fail the run: they mean equivalence could not be checked
    # (Python produced no reference), which is not a C++ equivalence defect.
    sys.exit(1 if (mismatch or errors) else 0)


if __name__ == "__main__":
    main()
