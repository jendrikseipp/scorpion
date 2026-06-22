#! /usr/bin/env python

"""
Compare preprocessor running time and peak memory across five revisions on the
autoscale-sat benchmarks.

Every revision's preprocess-h2 is run on byte-identical input: the task is
translated once with a single fixed translator (this checkout), then each
revision's binary preprocesses that SAS file. Peak memory (max RSS) is measured
externally with /usr/bin/time, so it is available uniformly -- including the
pre-revamp revision, which does not self-report it. The two time-limit flag
spellings (--h2_time_limit vs --h2-time-limit) are handled per revision.
"""

import os
from pathlib import Path

import custom_parser
import project

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from lab.experiment import Experiment

REPO = project.get_repo_base()
BENCHMARKS_DIR = Path(os.environ["AUTOSCALE_BENCHMARKS_SAT"])
assert BENCHMARKS_DIR.is_dir(), f"Benchmarks directory {BENCHMARKS_DIR} does not exist"
FD_DRIVER = str(REPO / "fast-downward.py")  # single fixed translator for all revisions
WRAPPER = str(project.DIR / "run-preprocess.sh")
REVISION_CACHE = (
    os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)

if project.REMOTE:
    ENV = project.TetralithEnvironment(
        memory_per_cpu="9G",
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-382",
    )
    SUITE = project.SUITE_AUTOSCALE
else:
    ENV = project.LocalEnvironment(processes=2)
    SUITE = ["depots:p01.pddl", "grid:p01.pddl", "gripper:p01.pddl"]

# (revision, short nick, h2 time-limit flag). The pre-revamp preprocessor uses
# the underscore spelling; the revamp onward uses hyphens.
REVISIONS = [
    ("221d2d4c", "01-pre-revamp", "--h2_time_limit"),
    ("23e6b053", "02-revamp", "--h2-time-limit"),
    ("72c7d494", "03-overflow-fix", "--h2-time-limit"),
    ("855e9800", "04-mutex-4gib", "--h2-time-limit"),
    ("bde8d34e", "05-simplified", "--h2-time-limit"),
]

ATTRIBUTES = [
    "error",
    "run_dir",
    "preprocessor_time",
    "peak_memory_kb",
    "preprocessor_operators",
    "preprocessor_task_size",
]

exp = Experiment(environment=ENV)

for rev, nick, h2flag in REVISIONS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, REPO, rev, [])
    cached_rev.cache()
    binary = str(Path(cached_rev.path) / "builds" / "release" / "bin" / "preprocess-h2")
    for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
        run = exp.add_run()
        run.add_command(
            "preprocess",
            [WRAPPER, FD_DRIVER, binary, h2flag, task.domain_file, task.problem_file],
            time_limit=1800,
            memory_limit=9000,
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

# Tables: per-revision preprocessor_time and peak_memory_kb (+ output-size sanity).
project.add_absolute_report(exp, attributes=ATTRIBUTES, filter=[project.group_domains])

# Relative scatter plots (new-vs-old) for both metrics, between consecutive
# revisions and pre-revamp -> simplified end-to-end.
project.RELATIVE = True
NICKS = [nick for _, nick, _ in REVISIONS]
pairs = list(zip(NICKS, NICKS[1:])) + [(NICKS[0], NICKS[-1])]
project.add_scatter_plot_reports(
    exp, pairs, ["preprocessor_time", "peak_memory_kb"], filter=[project.group_domains]
)

exp.run_steps()
