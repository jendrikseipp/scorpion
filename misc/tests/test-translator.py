#! /usr/bin/env python3


HELP = """\
Check that translator is deterministic.
Run the translator multiple times to test that the log and the output file are
the same for every run. Obviously, there might be false negatives, i.e.,
different runs might lead to the same nondeterministic results.
"""

import argparse
from collections import defaultdict
import itertools
import os
from pathlib import Path
import re
import subprocess
import sys


DIR = Path(__file__).resolve().parent
REPO = DIR.parents[1]
DRIVER = REPO / "fast-downward.py"
# Benchmarks are not bundled; point at a downward-benchmarks checkout via the
# DOWNWARD_BENCHMARKS environment variable (or pass a directory explicitly).
DEFAULT_BENCHMARKS = os.environ.get("DOWNWARD_BENCHMARKS")

# Default task set: the smallest task from each family that exposed a past
# py-vs-cpp divergence (the regression set). Kept small so the check is fast;
# these stress the determinism-prone code (invariant synthesis, axioms, fact
# groups). Pass an explicit suite to override.
DEFAULT_TASKS = [
    "assembly:prob01.pddl",
    "freecell:p01.pddl",
    "psr-large:p27-s172-n25-l2-f10.pddl",
    "psr-middle:p03-s28-n2-l5-f10.pddl",
    "settlers-sat18-adl:p01.pddl",
    "thoughtful-sat14-strips:bootstrap-typed-01.pddl",
    "trucks-strips:p05.pddl",
]


def parse_args():
    parser = argparse.ArgumentParser(description=HELP)
    parser.add_argument(
        "benchmarks_dir", nargs="?", default=DEFAULT_BENCHMARKS,
        help="path to benchmark directory (default: $DOWNWARD_BENCHMARKS)")
    parser.add_argument(
        "suite", nargs="*", default=DEFAULT_TASKS,
        help='task selection (default: the small per-family regression set). '
             'Use "all" to test all benchmarks, "first" to test the first task '
             'of each domain, or "<domain>:<problem>" for individual tasks')
    parser.add_argument(
        "--runs-per-task",
        help="translate each task this many times and compare the outputs",
        type=int, default=3)
    parser.add_argument(
        "--translator", choices=["py", "cpp"], default=None,
        help="which translator to test (default: the driver's default). "
             "Use 'cpp' to check determinism of the C++ translator.")
    args = parser.parse_args()
    if not args.benchmarks_dir:
        sys.exit("No benchmark directory: set the DOWNWARD_BENCHMARKS "
                 "environment variable or pass a directory explicitly.")
    args.benchmarks_dir = Path(args.benchmarks_dir).resolve()
    return args


def get_task_name(path):
    return "-".join(str(path).split("/")[-2:])


def translate_task(task_file, translator=None):
    print(f"Translate {get_task_name(task_file)}", flush=True)
    sys.stdout.flush()
    cmd = [sys.executable, str(DRIVER)]
    if translator:
        cmd += ["--translator", translator]
    cmd += ["--translate", str(task_file)]
    try:
        output = subprocess.check_output(cmd, encoding=sys.getfilesystemencoding())
    except OSError as err:
        sys.exit(f"Call failed: {' '.join(cmd)}\n{err}")
    # Remove information that may differ between calls.
    for pattern in [
            r"\[(.+s CPU, .+s wall-clock)\]",
            r"(\d+) KB",
            r"Planner time: (.+s)",
            ]:
        output = re.sub(pattern, "XXX", output)
    return output


def _get_all_tasks_by_domain(benchmarks_dir):
    # Ignore domains where translating the first task takes too much time or memory.
    # We also ignore citycar, which indeed reveals some nondeterminism in the
    # invariant synthesis. Fixing it would require to sort the actions which
    # seems to be detrimental on some other domains.
    blacklisted_domains = [
        "agricola-sat18-strips",
        "citycar-opt14-adl",  # cf. issue879
        "citycar-sat14-adl",  # cf. issue879
        "organic-synthesis-sat18-strips",
        "organic-synthesis-split-opt18-strips",
        "organic-synthesis-split-sat18-strips"]
    benchmarks_dir = Path(benchmarks_dir)
    tasks = defaultdict(list)
    domains = [
        domain_dir for domain_dir in benchmarks_dir.iterdir()
        if domain_dir.is_dir() and
        not str(domain_dir.name).startswith((".", "_", "unofficial")) and
        str(domain_dir.name) not in blacklisted_domains and
        # Skip container dirs (e.g. autoresearch/) that hold sub-suites rather
        # than problem files directly.
        any(f.is_file() and f.suffix == ".pddl" for f in domain_dir.iterdir())]
    for domain in domains:
        path = benchmarks_dir / domain
        tasks[domain] = [
            benchmarks_dir / domain / f
            for f in sorted(path.iterdir())
            if f.is_file() and "domain" not in str(f)]
    return sorted(tasks.values())


def get_tasks(args):
    suite = []
    for task in args.suite:
        if task == "first":
            # Add the first task of each domain.
            suite.extend([tasks[0] for tasks in _get_all_tasks_by_domain(args.benchmarks_dir)])
        elif task == "all":
            # Add the whole benchmark suite.
            suite.extend(itertools.chain.from_iterable(
                    tasks for tasks in _get_all_tasks_by_domain(args.benchmarks_dir)))
        else:
            # Add task from command line.
            task = task.replace(":", "/")
            suite.append(args.benchmarks_dir / task)
    return sorted(set(suite))


def cleanup():
    for f in Path(".").glob("translator-output-*.txt"):
        f.unlink()


def write_combined_output(output_file, task, translator=None):
    log = translate_task(task, translator)
    with open(output_file, "w") as combined_output:
        combined_output.write(log)
        with open("output.sas") as output_sas:
            combined_output.write(output_sas.read())


def main():
    args = parse_args()
    os.chdir(DIR)
    cleanup()
    for task in get_tasks(args):
        base_file = "translator-output-0.txt"
        write_combined_output(base_file, task, args.translator)
        for i in range(1, args.runs_per_task):
            compared_file = f"translator-output-{i}.txt"
            write_combined_output(compared_file, task, args.translator)
            files = [base_file, compared_file]
            try:
                subprocess.check_call(["diff", "-q"] + files)
            except subprocess.CalledProcessError:
                sys.exit(f"Error: Translator is nondeterministic for {task}.")
        print("Outputs match\n", flush=True)
        cleanup()


if __name__ == "__main__":
    main()
