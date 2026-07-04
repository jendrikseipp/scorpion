#! /usr/bin/env python

"""
Compare the C++ translator's running time and peak memory across every KEPT
revision of the optimization campaign, each against its predecessor revision.

The revisions below are the commits that changed src/translate-cpp/ code and
were kept (the reverted "release-each-action" pair is omitted; its net code
change is zero). "00-base" is the commit that first added the C++ translator --
the predecessor of the first optimization -- so consecutive pairs isolate the
effect of each individual change, and 00-base -> 33 shows the end-to-end result.

Each revision's translate-cpp is built standalone (the directory is
self-contained) into a per-SHA binary cache, so we do not rebuild the whole
Fast Downward search binary 34 times. Every revision runs on byte-identical
input (the same PDDL task); peak memory (max RSS) is measured externally with
/usr/bin/time so it is uniform across revisions. translator_task_size /
_operators are reported as a byte-equivalence sanity check (they must be
identical across all revisions).
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
BIN_CACHE = project.DIR / "data" / "translate-bin-cache"

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

# (nick, git SHA). Ordered oldest -> newest; numeric prefixes keep report
# columns and comparisons in campaign order. Predecessor of nick i is nick i-1.
REVISIONS = [
    ("00-base", "66a77f191"),
    ("01-role-dispatch", "257ae8f14"),
    ("02-int-join-keys", "77041c2ba"),
    ("03-fluent-pred-id", "d17be351b"),
    ("04-aux-flags", "28f7c120a"),
    ("05-inline-valuesets", "e7a24a544"),
    ("06-flat-varmap", "a4c3a408e"),
    ("07-unify-condmap", "775b2ec1a"),
    ("08-flatmap-effects", "0c44bd7a1"),
    ("09-flat-varmapping", "673e99d56"),
    ("10-int-fact-membership", "977401ea4"),
    ("11-drop-atomview", "990e9961a"),
    ("12-cache-pred-id", "173e04edf"),
    ("13-openaddr-dedup", "f3d62d5c8"),
    ("14-free-model", "a9609a031"),
    ("15-move-model-out", "9e72d3159"),
    ("16-uint32-hashes", "c9ddb50e6"),
    ("17-params-as-ids", "39512f7f6"),
    ("18-renumber-inplace", "5e537b114"),
    ("19-groundliteral", "d87059579"),
    ("20-merged-factmap", "d2d6fa464"),
    ("21-truthfalsity-singletons", "e3beda195"),
    ("22-polish-deadcode", "e6f391a10"),
    ("23-polish-comments", "069d0be1c"),
    ("24-polish-naming", "1fcec2131"),
    ("25-polish-hashcombine", "27d70ca66"),
    ("26-polish-literal-str", "3a318a1f5"),
    ("27-polish-prepost", "ba84a95d5"),
    ("28-polish-objects", "39900088b"),
    ("29-polish-d5-key", "dad603f27"),
    ("30-enqueue-once", "0ec80318f"),
    ("31-pne-key", "015944183"),
    ("32-groundeffect", "9e9166d4d"),
    ("33-process-merge", "c95f0386e"),
]

ATTRIBUTES = [
    "error",
    "run_dir",
    "coverage",
    "translator_time",
    "translator_wall_time",
    "peak_memory_kb",
    "translator_self_memory_kb",
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
    # Extract only src/translate-cpp at this revision (self-contained CMake).
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

# Absolute report: the whole progression (time, peak memory, output-shape
# sanity) with every revision as a column.
project.add_absolute_report(exp, attributes=ATTRIBUTES, filter=[project.group_domains])

# Each kept revision vs its predecessor (consecutive pairs) + end-to-end, in a
# single comparison table for runtime and peak memory.
NICKS = [nick for nick, _ in REVISIONS]
pairs = list(zip(NICKS, NICKS[1:])) + [(NICKS[0], NICKS[-1])]
project.add_comparative_report(
    exp,
    pairs,
    attributes=["translator_time", "peak_memory_kb", "translator_task_size"],
    filter=[project.group_domains],
    name=f"{exp.name}-compare",
)

# Relative scatter plots for the end-to-end effect (base -> HEAD).
project.RELATIVE = True
project.add_scatter_plot_reports(
    exp,
    [(NICKS[0], NICKS[-1])],
    ["translator_time", "peak_memory_kb"],
    filter=[project.group_domains],
)

exp.run_steps()
