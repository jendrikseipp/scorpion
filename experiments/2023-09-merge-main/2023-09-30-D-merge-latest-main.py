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
REVISION_CACHE = project.DIR / "data" / "revision-cache"
if project.REMOTE:
    ENV = project.TetralithEnvironment(
        email="jendrik.seipp@liu.se", extra_options="#SBATCH --account=naiss2023-5-314"
    )
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    ("cegar-scp-online", [
        "--search", """astar(scp_online([
            projections(sys_scp(max_time=60, max_time_per_restart=6)),
            cartesian()],
            saturator=perimstar, max_time=180, interval=1K, orders=greedy_orders()),
            pruning=limited_pruning(pruning=atom_centric_stubborn_sets(), min_required_pruning_ratio=0.2))"""]),
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
    ("9eb8e5652", "05-merge-main-before-cegar-renaming"),
    ("fee3a5f0d", "06-merge-latest-main"),
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
        ("05-merge-main-before-cegar-renaming-cegar-scp-online", "06-merge-latest-main-cegar-scp-online"),
    ],
    attributes=["total_time", "memory"],
)


exp.run_steps()
