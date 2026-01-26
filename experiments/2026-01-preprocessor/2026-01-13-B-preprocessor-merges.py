#! /usr/bin/env python
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
SUITE = ["depot:p01.pddl", "airport:p02-airport1-p1.pddl", "gripper:prob01.pddl"]
REVISION_CACHE = (
    os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
if project.REMOTE:
    # ENV = project.BaselSlurmEnvironment(email="my.name@myhost.ch")
    ENV = project.TetralithEnvironment(
        memory_per_cpu="9G",  # leave some space for the scripts
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-561",
    )
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

MAX_TIME_OUTER = 100 if project.REMOTE else 2
MAX_TIME_INNER = 10 if project.REMOTE else 1
SCORPION_PDBs = ["--search", f"astar(scp_online([projections(sys_scp(max_time={MAX_TIME_OUTER}, max_time_per_restart={MAX_TIME_INNER}, max_pdb_size=2M, max_collection_size=20M, pattern_type=interesting_non_negative))], saturator=perimstar, max_time={MAX_TIME_OUTER}, max_size=1M, interval=10K, orders=greedy_orders()))"]
SCORPION_CARTESIAN = ["--search", f"astar(scp_online([cartesian()], saturator=perimstar, max_time={MAX_TIME_OUTER}, max_size=1M, interval=10K, orders=greedy_orders()))"]

CONFIGS = [
    ("blind", ["--preprocess-options", "--h2-time-limit", "9999", "--search-options", "--search", "astar(blind())"]),
    ("blind-merge", ["--preprocess-options", "--h2-time-limit", "9999", "--merge-mutex-variables", "--search-options", "--search", "astar(blind())"]),
    ("scorpion-pdbs", ["--preprocess-options", "--h2-time-limit", "9999", "--search-options"] + SCORPION_PDBs),
    ("scorpion-pdbs-merge", ["--preprocess-options", "--h2-time-limit", "9999", "--merge-mutex-variables", "--search-options"] + SCORPION_PDBs),
    ("scorpion-cartesian", ["--preprocess-options", "--h2-time-limit", "9999", "--search-options"] + SCORPION_CARTESIAN),
    ("scorpion-cartesian-merge", ["--preprocess-options", "--h2-time-limit", "9999", "--merge-mutex-variables", "--search-options"] + SCORPION_CARTESIAN),
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
    ("9df4ff5e", ""),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "total_time",
    "h_values",
    "cost",
    "coverage",
    "expansions",
    "expansions_until_last_jump",
    "memory",
    project.EVALUATIONS_PER_TIME,
    "preprocessor_*",
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
