#! /usr/bin/env python

"""
Confirmation run for runs 64-66 of the autoresearch loop (sized gather
loops, probe construction time 0.609 -> 0.544; scaling budgets for the
conflict masks / static matrix / lookup rows on 1000+-abstraction tasks).

Single revision on all optimal STRIPS benchmarks with the same limits as
the previous confirmation grids, so coverage is directly comparable.
"""

import os

import custom_parser
import project

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment

REPO = project.get_repo_base()
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
SUITE = ["depot:p01.pddl", "grid:prob01.pddl", "gripper:prob01.pddl"]
REVISION_CACHE = (
    os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
if project.REMOTE:
    ENV = project.TetralithEnvironment(
        memory_per_cpu="9G",  # leave some space for the scripts
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-382",
    )
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    (
        "sscp-sys2",
        [
            "--search",
            "astar(sscp(structured_order_generator="
            "structured_order_generator_full(abstraction_generators="
            "[projections(systematic(2))])))",
        ],
    ),
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--validate",
    "--overall-time-limit",
    "30m",
    "--overall-memory-limit",
    "8G",
]
# Pairs of revision identifier and optional revision nick.
REV_NICKS = [
    ("7fdf9ab6", "09-final"),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "total_time",
    "coverage",
    "expansions",
    "memory",
    "initial_h_value",
    "time_for_computing_patterns",
    "time_for_building_projections",
    "sscp_dag_time",
    "search_time",
    "sscp_lookup_cache_hits",
    "sscp_recomputed_lookup_tables",
    "sscp_generated_nodes",
    "sscp_instructions",
    "sscp_stored_cost_functions",
    project.EVALUATIONS_PER_TIME,
]

exp = Experiment(environment=ENV)
for rev, rev_nick in REV_NICKS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, REPO, rev, BUILD_OPTIONS)
    cached_rev.cache()
    exp.add_resource("", cached_rev.path, cached_rev.get_relative_exp_path())
    for config_nick, config in CONFIGS:
        algo_name = rev_nick
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

exp.run_steps()
