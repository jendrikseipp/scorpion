#! /usr/bin/env python3

import os
from pathlib import Path

from downward.experiment import FastDownwardExperiment

import custom_parser
import project

SUITE = project.SUITE_SATISFICING
ENV = project.TetralithEnvironment(email="jendrik.seipp@liu.se", memory_per_cpu="9G", extra_options="#SBATCH --account=naiss2024-5-421")
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
REPO = "/proj/dfsplan/users/x_jense/scorpion-dev"
DIR = Path(__file__).parent
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
# If REVISION_CACHE is None, the default ./data/revision-cache is used.
REVISION_CACHE = None
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "search_time",
    "total_time",
    "cost",
    "coverage",
    "expansions_until_last_jump",
    "memory",
    "initial_h_value",
]

if not project.REMOTE:
    ENV = project.LocalEnvironment(processes=2)
    REPO = Path.home() / "projects" / "Downward" / "scorpion-dev"
    SUITE = ["gripper:prob01.pddl"]

exp = FastDownwardExperiment(environment=ENV, revision_cache=REVISION_CACHE)

# Add built-in parsers to the experiment.
exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)
exp.add_parser(custom_parser.get_parser())

exp.add_suite(BENCHMARKS_DIR, SUITE)
DRIVER_OPTIONS = [
    "--overall-time-limit",
    "5m",
    "--overall-memory-limit",
    "8G",
    "--transform-task",
    "preprocess-h2",
]

CONFIGS = [
    ("lama-first-base", "0c7bb1d7b", ["--alias", "lama-first", "--transform-task-options", "h2_time_limit,180"], []),
    ("lama-first-h2-early-exit", "31aac2915", ["--alias", "lama-first"], ["--transform-options", "--h2_time_limit", "180"]),
]

for name, rev, extra_driver_options, config in CONFIGS:
    exp.add_algorithm(
        name,
        REPO,
        rev,
        config,
        driver_options=DRIVER_OPTIONS + extra_driver_options,
    )

# Add step that writes experiment files to disk.
exp.add_step("build", exp.build)

# Add step that executes all runs.
exp.add_step("start", exp.start_runs)

# Add parse step
exp.add_step("parse", exp.parse)

exp.add_fetcher(name="fetch")

project.add_absolute_report(exp, attributes=ATTRIBUTES)

if not project.REMOTE:
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)

# Parse the commandline and show or run experiment steps.
exp.run_steps()
