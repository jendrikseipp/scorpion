#! /usr/bin/env python

import os
from pathlib import Path

import custom_parser
import project

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment

REPO = project.get_repo_base()
BENCHMARKS_DIR = Path(os.environ["AUTOSCALE_BENCHMARKS_OPT"])
assert BENCHMARKS_DIR.is_dir(), f"Benchmarks directory {BENCHMARKS_DIR} does not exist"
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
SUITE = ["depots:p01.pddl", "grid:p01.pddl", "gripper:p01.pddl"]
REVISION_CACHE = (
    os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
if project.REMOTE:
    # ENV = project.BaselSlurmEnvironment(email="my.name@myhost.ch")
    ENV = project.TetralithEnvironment(
        memory_per_cpu="9G",  # leave some space for the scripts
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-382",
    )
    SUITE = project.SUITE_AUTOSCALE
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    ("blind", ["--preprocess-options", "--h2-time-limit", "9999", "--search-options", "--search", """astar(scp([
      projections(systematic(2)),
      cartesian()],
      saturator=perimstar, max_orders=100, max_time=infinity, max_optimization_time=0, diversify=false, orders=greedy_orders()),
      pruning=limited_pruning(pruning=atom_centric_stubborn_sets(), min_required_pruning_ratio=0.2))"""

]),
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--preprocess",
    "--validate",
    "--overall-time-limit",
    "30m",
    "--overall-memory-limit",
    "8G",
]

# Pairs of revision identifier and optional revision nick.
REV_NICKS = [
    ("7d742a2d", "01-base"),
    ("703259d8", "02-no-endl-sas-write"),
    ("776ae435", "03-remove-duplicate-operators-preprocessor"),
    ("cdd227ec", "04-remove-duplicate-operators-translator"),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "total_time",
    "h_values",
    "coverage",
    "expansions",
    "memory",
    project.EVALUATIONS_PER_TIME,
    "preprocessor_*",
    "translator_time_done",
]

exp = Experiment(environment=ENV)
for rev, rev_nick in REV_NICKS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, REPO, rev, BUILD_OPTIONS)
    cached_rev.cache()
    exp.add_resource("", cached_rev.path, cached_rev.get_relative_exp_path())

    for config_nick, config in CONFIGS:
        myconfig = config.copy()
        if "base" in rev_nick:
            myconfig = [opt.replace("--h2-time-limit", "--h2_time_limit") for opt in myconfig]
        algo_name = f"{rev_nick}-{config_nick}" if rev_nick else config_nick

        for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
            algo = FastDownwardAlgorithm(
                algo_name,
                cached_rev,
                DRIVER_OPTIONS,
                myconfig,
            )
            run = FastDownwardRun(exp, algo, task)
            exp.add_run(run)


exp.add_parser(project.FastDownwardExperiment.EXITCODE_PARSER)
exp.add_parser(project.FastDownwardExperiment.TRANSLATOR_PARSER)
exp.add_parser(project.FastDownwardExperiment.SINGLE_SEARCH_PARSER)
exp.add_parser(custom_parser.get_parser())
exp.add_parser(project.FastDownwardExperiment.PLANNER_PARSER)

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_fetcher(name="fetch")

project.add_absolute_report(
    exp,
    attributes=ATTRIBUTES,
    filter=[project.add_evaluations_per_time, project.group_domains],
)
if not project.REMOTE:
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)
project.add_compress_exp_dir_step(exp)

project.add_scatter_plot_reports(exp, [
        ("01-base-blind", "02-no-endl-sas-write-blind"),
        ("02-no-endl-sas-write-blind", "03-remove-duplicate-operators-preprocessor"),
        ("03-remove-duplicate-operators-preprocessor", "04-remove-duplicate-operators-translator"),
    ], ["preprocessor_time"])

exp.run_steps()
