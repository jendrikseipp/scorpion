#! /usr/bin/env python

import os
import shutil

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment

from labreports import PerTaskComparison

import project


REPO = project.get_repo_base()
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense"
BOUNDS_FILE = "bounds.json"
SUITE = ["depot:p01.pddl", "grid:prob01.pddl", "gripper:prob01.pddl"]
# If REVISION_CACHE is None, the default "./data/revision-cache/" is used.
REVISION_CACHE = os.environ.get("DOWNWARD_REVISION_CACHE")
if project.REMOTE:
    ENV = project.TetralithEnvironment(
        email="jendrik.seipp@liu.se", extra_options="#SBATCH --account=snic2023-5-341"
    )
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    ("cegar-scp-online", ["--search", "astar(scp_online([cartesian()]))"]),
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--validate",
    "--overall-time-limit",
    "5m",
    "--overall-memory-limit",
    "3584M",
]
# Pairs of revision identifier and revision nick.
REV_NICKS = [
    ("4a083b2ef", "01-reduce-value-conversions"),
    #("9dd563534", "02-merge-main"),  # build broken because SlimPDB files are missing.
    ("3861241ab", "03-merge-scorpion-branches"),
    ("c182d3328", "04-merge-scorpion-branches-again"),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "search_time",
    "score_search_time",
    "total_time",
    "coverage",
    "expansions",
    "expansions_until_last_jump",
    "memory",
    project.EVALUATIONS_PER_TIME,
]

exp = Experiment(environment=ENV)
for rev, rev_nick in REV_NICKS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, REPO, rev, BUILD_OPTIONS)
    cached_rev.cache()
    exp.add_resource("", cached_rev.path, cached_rev.get_relative_exp_path())
    for config_nick, config in CONFIGS:
        algo_name = f"{rev_nick}-{config_nick}" if rev_nick else config_nick

        for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
            algo = FastDownwardAlgorithm(
                algo_name,
                cached_rev,
                DRIVER_OPTIONS,
                config,
            )
            run = FastDownwardRun(exp, algo, task)
            exp.add_run(run)

exp.add_parser(project.FastDownwardExperiment.EXITCODE_PARSER)
exp.add_parser(project.FastDownwardExperiment.TRANSLATOR_PARSER)
exp.add_parser(project.FastDownwardExperiment.SINGLE_SEARCH_PARSER)
exp.add_parser(project.DIR / "parser.py")
exp.add_parser(project.FastDownwardExperiment.PLANNER_PARSER)

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_fetcher(name="fetch")

if not project.REMOTE:
    exp.add_step("remove-eval-dir", shutil.rmtree, exp.eval_dir, ignore_errors=True)
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)

project.add_absolute_report(
    exp,
    attributes=ATTRIBUTES,
    filter=[project.add_evaluations_per_time, project.group_domains],
)

exp.add_report(PerTaskComparison(
        attributes=["search_time"]))

project.add_scatter_plot_reports(
    exp,
    algorithm_pairs=[
        ("02-skip-conversion-if-no-abstracted-vars:cegar-scp-online", "03-value-map-class:cegar-scp-online"),
        ("03-value-map-class:cegar-scp-online", "04-small-vector:cegar-scp-online"),
        ("04-small-vector:cegar-scp-online", "05-abs-var-struct:cegar-scp-online"),
        ("02-skip-conversion-if-no-abstracted-vars:cegar-scp-online", "05-abs-var-struct:cegar-scp-online"),
    ],
    attributes=["search_time", "memory"],
)


exp.run_steps()
