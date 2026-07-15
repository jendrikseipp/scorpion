#! /usr/bin/env python

"""
Compare the structured SCP (perfect SCP via ADHG) heuristic across the key
revisions of the autoresearch loop, on all optimal STRIPS benchmarks:

  01-integration    faithful port of the structured-scp branch
  02-fast           end of the speed segment (~5x faster construction)
  03-conflicts      intersection-based conflicts (Theorems 6/7; DAG collapse)
  04-table-sharing  lookup tables shared across cost functions
  05-final          latest revision after memory work, audit and cleanup

All revisions accept the same configuration string, so every run uses
identical options: A* with sscp over systematic(2) projections, 30 minutes
and 8 GiB, with plan validation.
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
    ("edcb285f", "01-integration"),
    ("ce63324b", "02-fast"),
    ("142b8958", "03-conflicts"),
    ("42f02ccf", "04-table-sharing"),
    ("a5255809", "05-final"),
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
