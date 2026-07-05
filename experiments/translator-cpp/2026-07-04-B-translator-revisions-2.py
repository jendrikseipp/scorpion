#! /usr/bin/env python

"""
Second translator-revisions experiment: the configurations landed AFTER the
first experiment's endpoint (33-process-merge). These are rounds 3-4 plus the
untested-idea sweep -- R5, R1, R4, W3, W1+W2a, W4, R3.

The chain 33..40 lets each new configuration be compared against its
predecessor. 00-base (the commit that first added the C++ translator) is also
included so the artifact can draw the OVERALL before/after scatter (00-base ->
40-index-sort) for runtime and peak memory across the whole campaign.

Same method as experiment 1: each revision's translate-cpp is built standalone
into a per-SHA binary cache (shared with experiment 1, so 00-base and
33-process-merge are already built), every revision runs on byte-identical PDDL
input, peak RSS is measured externally with /usr/bin/time, and task-size/
operators ride along as a byte-equivalence check.
"""

import os
import shutil
import subprocess
from pathlib import Path

import custom_parser
import project

from downward import suites
from lab.experiment import Experiment

REPO = project.get_repo_base()
BENCHMARKS_DIR = Path(os.environ["AUTOSCALE_BENCHMARKS_SAT"])
assert BENCHMARKS_DIR.is_dir(), f"Benchmarks directory {BENCHMARKS_DIR} does not exist"
WRAPPER = str(project.DIR / "run-translate.sh")
# Reuse experiment 1's binary cache (keyed by SHA) so shared revisions are not
# rebuilt.
BIN_CACHE = (
    REPO / "experiments" / "2026-07-04-translator-revisions" / "data"
    / "translate-bin-cache"
)

if project.REMOTE:
    ENV = project.TetralithEnvironment(
        memory_per_cpu="8G",
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-382",
    )
    SUITE = project.SUITE_AUTOSCALE
else:
    ENV = project.LocalEnvironment(processes=4)
    SUITE = ["depots:p01.pddl", "gripper:p01.pddl", "rovers:p01.pddl"]

# (nick, git SHA). 00-base and 33-process-merge anchor the chain to experiment 1;
# 34..40 are the new configurations, in campaign order. Predecessor of nick i is
# the row above it, so consecutive pairs isolate each new change; 00 -> 40 is the
# whole-campaign before/after.
REVISIONS = [
    ("00-base", "66a77f191"),
    ("33-process-merge", "c95f0386e"),
    ("34-r5-balancechecker", "1a9f6c4a9"),
    ("35-r1-actions-byvalue", "d228fba8f"),
    ("36-r4-flat-factmap", "c18fd4b1d"),
    ("37-w3-weight-copy-elim", "cad44ecdc"),
    ("38-w1-flat-dtg", "28486f0e4"),
    ("39-w4-product-recursion", "97265acef"),
    ("40-r3-index-sort", "e91fc3da6"),
]

ATTRIBUTES = [
    "error",
    "run_dir",
    "coverage",
    "translator_time",
    "translator_wall_time",
    "peak_memory_kb",
    "translator_task_size",
    "translator_variables",
    "translator_operators",
    "translator_axioms",
]


def build_binary(sha):
    """Build (once) revision *sha*'s translate-cpp; return its binary path."""
    out_bin = BIN_CACHE / sha / "translate-cpp"
    if out_bin.is_file():
        return str(out_bin)
    print(f"Building translate-cpp for {sha} ...", flush=True)
    src = BIN_CACHE / sha / "src"
    build = BIN_CACHE / sha / "build"
    shutil.rmtree(BIN_CACHE / sha, ignore_errors=True)
    src.mkdir(parents=True)
    archive = subprocess.run(
        ["git", "-C", str(REPO), "archive", f"{sha}:src/translate-cpp"],
        check=True, stdout=subprocess.PIPE,
    )
    subprocess.run(["tar", "-x", "-C", str(src)], input=archive.stdout, check=True)
    subprocess.check_call(
        ["cmake", "-S", str(src), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"],
        stdout=subprocess.DEVNULL,
    )
    subprocess.check_call(
        ["cmake", "--build", str(build), "-j", "8"], stdout=subprocess.DEVNULL
    )
    shutil.copy2(build / "translate-cpp", out_bin)
    shutil.rmtree(src, ignore_errors=True)
    shutil.rmtree(build, ignore_errors=True)
    return str(out_bin)


BINARIES = {sha: build_binary(sha) for _, sha in REVISIONS}

exp = Experiment(environment=ENV)

for nick, sha in REVISIONS:
    binary = BINARIES[sha]
    for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
        run = exp.add_run()
        run.add_command(
            "translate",
            [WRAPPER, binary, task.domain_file, task.problem_file],
            time_limit=1800,
            memory_limit=8000,
        )
        run.set_property("domain", task.domain)
        run.set_property("problem", task.problem)
        run.set_property("algorithm", nick)
        run.set_property("id", [nick, task.domain, task.problem])

exp.add_parser(custom_parser.get_parser())

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_fetcher(name="fetch")

# Absolute report over all revisions.
project.add_absolute_report(exp, attributes=ATTRIBUTES, filter=[project.group_domains])

# Each new configuration vs its predecessor (33..40) + 00-base -> 40 end-to-end.
NICKS = [nick for nick, _ in REVISIONS]
chain = NICKS[1:]  # 33-process-merge .. 40-index-sort
pairs = list(zip(chain, chain[1:])) + [(NICKS[0], NICKS[-1])]
project.add_comparative_report(
    exp,
    pairs,
    attributes=["translator_time", "peak_memory_kb", "translator_task_size"],
    filter=[project.group_domains],
    name=f"{exp.name}-compare",
)

# OVERALL before/after relative scatter plots (00-base -> 40-index-sort) for
# runtime and peak memory -- the whole-campaign effect.
project.RELATIVE = True
project.add_scatter_plot_reports(
    exp,
    [(NICKS[0], NICKS[-1])],
    ["translator_time", "peak_memory_kb"],
    filter=[project.group_domains],
)

exp.run_steps()
