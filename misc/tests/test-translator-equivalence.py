#! /usr/bin/env python3

HELP = """\
Check byte-for-byte equivalence of the two translator variants.

For every task in a benchmark suite, run the Python translator
(src/translate/, --translator py) and the C++ translator
(src/translate-cpp/, --translator cpp) and compare their output.sas
byte-for-byte. With the C++ translator's default CPython-compatible RNG
(see src/translate-cpp/utils/cpython_random.h) the two should produce
identical output on every task; any mismatch is reported and fails the run.

By default the bundled tasks under misc/tests/benchmarks are used, but any
other benchmark directory can be passed (e.g. downward-benchmarks). Tasks are
discovered recursively, so both the flat domain/problem layout and nested
layouts (e.g. the autoresearch subset) are handled.

Requires the C++ translator to be built:
    ./build.py release --with-translate-cpp

Examples:
    ./test-translator-equivalence.py
    ./test-translator-equivalence.py /path/to/downward-benchmarks gripper:prob01.pddl
    ./test-translator-equivalence.py misc/tests/benchmarks all
"""

import argparse
import filecmp
import os
from pathlib import Path
import subprocess
import sys
import tempfile

DIR = Path(__file__).resolve().parent
REPO = DIR.parents[1]
DRIVER = REPO / "fast-downward.py"
DEFAULT_BENCHMARKS = REPO / "misc" / "tests" / "benchmarks"


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
    cmd = [sys.executable, str(DRIVER), "--translator", translator,
           "--translate", str(domain), str(problem)]
    proc = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, encoding="utf-8")
    return proc.returncode, proc.stderr


def main():
    p = argparse.ArgumentParser(
        description=HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("benchmarks_dir", nargs="?", default=str(DEFAULT_BENCHMARKS),
                   help="benchmark directory (default: misc/tests/benchmarks)")
    p.add_argument("suite", nargs="*", default=["all"],
                   help='"all" (default), "first" (first task per domain), '
                        'or "<domain>:<problem>" entries')
    args = p.parse_args()
    benchmarks_dir = Path(args.benchmarks_dir).resolve()
    if not benchmarks_dir.is_dir():
        sys.exit(f"Not a directory: {benchmarks_dir}")

    tasks = select(discover_tasks(benchmarks_dir), args.suite, benchmarks_dir)
    if not tasks:
        sys.exit(f"No tasks found under {benchmarks_dir}")

    print(f"Comparing py vs cpp translator output on {len(tasks)} task(s) "
          f"from {benchmarks_dir}\n")
    identical, mismatch, errors = [], [], []
    for domain, problem in tasks:
        name = f"{problem.parent.name}:{problem.name}"
        with tempfile.TemporaryDirectory() as tmp:
            pyd, cppd = Path(tmp) / "py", Path(tmp) / "cpp"
            pyd.mkdir(); cppd.mkdir()
            rc_py, err_py = translate("py", domain, problem, pyd)
            rc_cpp, err_cpp = translate("cpp", domain, problem, cppd)
            py_sas, cpp_sas = pyd / "output.sas", cppd / "output.sas"
            if rc_py != 0 or not py_sas.exists():
                errors.append((name, "py failed")); print(f"ERROR  {name} (py)")
            elif rc_cpp != 0 or not cpp_sas.exists():
                errors.append((name, "cpp failed")); print(f"ERROR  {name} (cpp)")
            elif filecmp.cmp(py_sas, cpp_sas, shallow=False):
                identical.append(name); print(f"ok     {name}")
            else:
                mismatch.append(name); print(f"DIFFER {name}")

    print(f"\nsummary: {len(identical)} identical, {len(mismatch)} differ, "
          f"{len(errors)} error(s) of {len(tasks)} tasks")
    if mismatch:
        print("byte-differing tasks:")
        for n in mismatch:
            print(f"  {n}")
    if errors:
        print("errored tasks:")
        for n, why in errors:
            print(f"  {n}: {why}")
    sys.exit(1 if (mismatch or errors) else 0)


if __name__ == "__main__":
    main()
