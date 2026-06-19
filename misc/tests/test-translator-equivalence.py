#! /usr/bin/env python3

HELP = """\
Check byte-for-byte equivalence of the two translator variants.

For every task in a benchmark suite, run the Python translator
(src/translate/, --translator py) and the C++ translator (src/translate-cpp/,
--translator cpp) and compare their output.sas byte-for-byte. With the C++
translator's default CPython-compatible RNG (see
src/translate-cpp/utils/cpython_random.h) the two produce identical output on
every task; any mismatch is reported and fails the run.

This checks only py-vs-cpp equivalence. Determinism of each translator is
checked separately by test-translator.py (pass --translator cpp for the C++
variant).

By default only a small, fast regression set is checked: the smallest task
from each family that exposed a past py-vs-cpp divergence -- assembly, freecell,
psr-large, psr-middle, settlers-sat18-adl, thoughtful-sat14-strips and
trucks-strips (axiom/mutex/sort divergences fixed during cleanup), plus
ged-positional and philosophers (invariant-RNG path) and miconic and logistics
(MaxDAG variable ordering) fixed during the port. Pass an explicit suite ("all"
or "first") and/or a different benchmark directory to check more; tasks are
discovered recursively, so both the flat domain/problem layout and nested
layouts are handled.

Requires the C++ translator to be built:
    ./build.py release --with-translate-cpp

Examples:
    ./test-translator-equivalence.py
    ./test-translator-equivalence.py misc/tests/benchmarks all
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
DEFAULT_BENCHMARKS = REPO / "misc" / "tests" / "benchmarks"

# Default task set: the smallest task from each family that exposed a past
# py-vs-cpp divergence (the regression set). Kept small so the check is fast;
# pass an explicit suite ("all", "first", or "<family>:<problem>") to override.
DEFAULT_TASKS = [
    "assembly:prob01.pddl",
    "freecell:p01.pddl",
    "ged-positional:d-1-3.pddl",
    "logistics:p01.pddl",
    "miconic:s1-0.pddl",
    "philosophers:p01-phil2.pddl",
    "psr-large:p27-s172-n25-l2-f10.pddl",
    "psr-middle:p03-s28-n2-l5-f10.pddl",
    "settlers-sat18-adl:p01.pddl",
    "thoughtful-sat14-strips:bootstrap-typed-01.pddl",
    "trucks-strips:p05.pddl",
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


def translate(translator, domain, problem, cwd):
    """Run one translator; return (returncode, wall-clock seconds)."""
    cmd = [sys.executable, str(DRIVER), "--translator", translator,
           "--translate", str(domain), str(problem)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, encoding="utf-8")
    return proc.returncode, time.perf_counter() - start


def check_one(domain, problem):
    """Translate one task with both variants and compare. Returns
    (name, status, py_time, cpp_time, detail); status in ok/differ/error."""
    name = f"{problem.parent.name}:{problem.name}"
    with tempfile.TemporaryDirectory() as tmp:
        pyd, cppd = Path(tmp) / "py", Path(tmp) / "cpp"
        pyd.mkdir(); cppd.mkdir()
        rc_py, py_time = translate("py", domain, problem, pyd)
        rc_cpp, cpp_time = translate("cpp", domain, problem, cppd)
        py_sas, cpp_sas = pyd / "output.sas", cppd / "output.sas"
        if rc_py != 0 or not py_sas.exists():
            status, detail = "error", "py failed"
        elif rc_cpp != 0 or not cpp_sas.exists():
            status, detail = "error", "cpp failed"
        elif filecmp.cmp(py_sas, cpp_sas, shallow=False):
            status, detail = "ok", ""
        else:
            status, detail = "differ", ""
    return name, status, py_time, cpp_time, detail


def main():
    p = argparse.ArgumentParser(
        description=HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("benchmarks_dir", nargs="?", default=str(DEFAULT_BENCHMARKS),
                   help="benchmark directory (default: "
                        "misc/tests/benchmarks)")
    p.add_argument("suite", nargs="*", default=DEFAULT_TASKS,
                   help='task selection (default: the small per-family '
                        'regression set). "all", "first" (first task per '
                        'domain), or "<domain>:<problem>" entries.')
    p.add_argument("-j", "--jobs", type=int,
                   default=min(os.cpu_count() or 4, 8),
                   help="number of tasks to translate in parallel "
                        "(default: min(cpu_count, 8)). Note: per-task times "
                        "are wall-clock and inflate under parallelism; the "
                        "elapsed line reflects the real speedup.")
    args = p.parse_args()
    benchmarks_dir = Path(args.benchmarks_dir).resolve()
    if not benchmarks_dir.is_dir():
        sys.exit(f"Not a directory: {benchmarks_dir}")

    tasks = select(discover_tasks(benchmarks_dir), args.suite, benchmarks_dir)
    if not tasks:
        sys.exit(f"No tasks found under {benchmarks_dir}")

    jobs = max(1, min(args.jobs, len(tasks)))
    print(f"Comparing py vs cpp translator output on {len(tasks)} task(s) "
          f"from {benchmarks_dir} ({jobs} parallel job(s))\n")
    identical, mismatch, errors = [], [], []
    py_total = cpp_total = 0.0
    start = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        futures = [ex.submit(check_one, d, p) for d, p in tasks]
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
            else:
                errors.append((name, detail))
                print(f"ERROR  {name} ({detail})  [{timing}]", flush=True)
    elapsed = time.perf_counter() - start

    speedup = (py_total / cpp_total) if cpp_total else float("nan")
    print(f"\nsummed translate time (per-task wall-clock): py {py_total:.2f}s, "
          f"cpp {cpp_total:.2f}s ({speedup:.2f}x); elapsed {elapsed:.2f}s")
    print(f"summary: {len(identical)} identical, {len(mismatch)} differ, "
          f"{len(errors)} error(s) of {len(tasks)} tasks")
    if mismatch:
        print("byte-differing tasks:")
        for n in sorted(mismatch):
            print(f"  {n}")
    if errors:
        print("errored tasks:")
        for n, why in sorted(errors):
            print(f"  {n}: {why}")
    sys.exit(1 if (mismatch or errors) else 0)


if __name__ == "__main__":
    main()
