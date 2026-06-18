#! /usr/bin/env python3
"""Benchmark the C++ translator directly (no driver) on the benchmark suite.

Runs `translate-cpp DOMAIN.pddl PROBLEM.pddl` once per task, measuring the
translator's own CPU time (user+system) via os.wait4 rusage -- this excludes
Python/shell/build overhead and is what we optimize.

Three modes:
  (default)        warmup + timed reps; prints one `METRIC total_cpu=<sec>`
                   line per timed rep (sum of per-task CPU over all tasks).
  --save-sas DIR   run each task once, write <task>.sas into DIR (references).
  --check DIR      run each task once, compare output to <task>.sas in DIR;
                   exit non-zero on the first mismatch (correctness gate).

The benchmark set and per-task work must never change inside an experiment --
edit the translator, not this script.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DIR = Path(__file__).resolve().parent
REPO = DIR.parents[1]
DEFAULT_BENCH = DIR / "benchmarks"
DEFAULT_BIN = REPO / "builds" / "release" / "bin" / "translate-cpp"


def discover_tasks(bench_dir, only=None):
    """Each task dir holds domain.pddl and one or more problem *.pddl files.

    A dir with a single problem yields one task named after the dir; a dir with
    several problems yields one task per problem named "<dir>-<problem-stem>".
    If *only* is given (a set of task-dir names), restrict to those dirs --
    used for a fast runtime guard on a representative heavy-task subset.
    """
    tasks = []
    for sub in sorted(p for p in bench_dir.iterdir() if p.is_dir()):
        if only is not None and sub.name not in only:
            continue
        domain = sub / "domain.pddl"
        problems = [p for p in sorted(sub.glob("*.pddl")) if p.name != "domain.pddl"]
        if not domain.exists() or not problems:
            sys.exit(f"bad task dir {sub}: need domain.pddl + 1+ problem .pddl")
        for prob in problems:
            name = sub.name if len(problems) == 1 else f"{sub.name}-{prob.stem}"
            tasks.append((name, domain, prob))
    if not tasks:
        sys.exit(f"no tasks found under {bench_dir}")
    if only is not None:
        missing = only - {name for name, _, _ in tasks}
        if missing:
            sys.exit(f"requested tasks not found: {sorted(missing)}")
    return tasks


def run_one(bin_path, domain, problem, out_sas=None):
    """Run the translator in a temp cwd. Return CPU seconds (user+sys) of the
    child process. Optionally copy its output.sas to out_sas."""
    with tempfile.TemporaryDirectory() as tmp:
        cmd = [str(bin_path), str(domain), str(problem)]
        proc = subprocess.Popen(cmd, cwd=tmp,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        _, status, rusage = os.wait4(proc.pid, 0)
        if status != 0:
            raise RuntimeError(f"translator failed (status={status}) on {problem}")
        produced = Path(tmp) / "output.sas"
        if not produced.exists():
            raise RuntimeError(f"no output.sas produced for {problem}")
        if out_sas is not None:
            shutil.copyfile(produced, out_sas)
        return rusage.ru_utime + rusage.ru_stime


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(DEFAULT_BIN))
    ap.add_argument("--bench", default=str(DEFAULT_BENCH))
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--save-sas", metavar="DIR",
                    help="run once; save each task's output.sas as <task>.sas in DIR")
    ap.add_argument("--check", metavar="DIR",
                    help="run once; compare each output to reference <task>.sas in DIR")
    ap.add_argument("--tasks", metavar="NAMES",
                    help="comma-separated task-dir names to restrict to (e.g. a "
                         "heavy-task subset for a fast runtime guard)")
    args = ap.parse_args()

    bin_path = Path(args.bin).resolve()
    if not bin_path.exists():
        sys.exit(f"translator binary not found: {bin_path}")
    only = set(args.tasks.split(",")) if args.tasks else None
    tasks = discover_tasks(Path(args.bench).resolve(), only=only)

    if args.save_sas:
        out = Path(args.save_sas).resolve()
        out.mkdir(parents=True, exist_ok=True)
        for name, domain, problem in tasks:
            run_one(bin_path, domain, problem, out_sas=out / f"{name}.sas")
            print(f"saved {name}.sas", flush=True)
        return

    if args.check:
        # Correctness gate. Each task must either be byte-identical to its
        # reference (preferred) or, failing that, canonically equivalent
        # (same task up to variable renaming/reordering -- verified by
        # tests/canonical_diff.py). A task that is only canonically equal is
        # a signal that adding a canonicalizing sort to the output could
        # restore byte-identity. Anything else fails the gate.
        ref = Path(args.check).resolve()
        canon = REPO / "src" / "translate-cpp" / "tests" / "canonical_diff.py"
        byte_ok, canon_ok, bad = [], [], []
        with tempfile.TemporaryDirectory() as tmp:
            for name, domain, problem in tasks:
                cur = Path(tmp) / f"{name}.sas"
                run_one(bin_path, domain, problem, out_sas=cur)
                refsas = ref / f"{name}.sas"
                if not refsas.exists():
                    sys.exit(f"missing reference {refsas}; regenerate references")
                if sha(cur) == sha(refsas):
                    byte_ok.append(name)
                    print(f"byte-ok {name}", flush=True)
                    continue
                r = subprocess.run([sys.executable, str(canon), str(refsas),
                                    str(cur)], capture_output=True, text=True)
                if r.returncode == 0:
                    canon_ok.append(name)
                    print(f"canonical-ok {name}", flush=True)
                else:
                    bad.append(name)
                    print(f"MISMATCH {name}\n{r.stdout}{r.stderr}", flush=True)
        print(f"summary: {len(byte_ok)} byte-identical, "
              f"{len(canon_ok)} canonical-only, {len(bad)} mismatched")
        if bad:
            sys.exit(f"{len(bad)} task(s) not equivalent to reference: {bad}")
        if canon_ok:
            print(f"all equivalent; canonical-only (a sort may restore "
                  f"byte-identity): {canon_ok}")
        else:
            print("all outputs match reference (byte-identical)")
        return

    # Timing mode: warmup (discarded) then timed reps.
    for _ in range(args.warmup):
        for _, domain, problem in tasks:
            run_one(bin_path, domain, problem)
    for _ in range(args.reps):
        total = 0.0
        for _, domain, problem in tasks:
            total += run_one(bin_path, domain, problem)
        print(f"METRIC total_cpu={total:.3f}", flush=True)


if __name__ == "__main__":
    main()
