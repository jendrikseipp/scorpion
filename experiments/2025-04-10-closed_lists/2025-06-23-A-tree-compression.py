''#! /usr/bin/env python

import os
from pathlib import Path

from downward.experiment import FastDownwardExperiment, FastDownwardAlgorithm, FastDownwardRun
from downward.suites import build_suite
from downward.cached_revision import CachedFastDownwardRevision
from lab.experiment import Experiment
from lab.environments import TetralithEnvironment, LocalEnvironment
from benchmarks import *
import custom_parser
import project

REVISION_CACHE = (
        os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
BUILD_OPTIONS = []
if project.REMOTE:
    ENV = TetralithEnvironment(
        setup=TetralithEnvironment.DEFAULT_SETUP,
        email="olijo92@liu.se",
        extra_options="#SBATCH -A naiss2024-5-421",
        memory_per_cpu="9G",
    )
    TIME_LIMIT = 15 * 60
    MEMORY_LIMIT = "8G"

    SUITE = build_suite(os.environ.get("DOWNWARD_BENCHMARKS"), SUITE_IPC_OPTIMAL_STRIPS)
else:
    ENV = LocalEnvironment(processes=1)
    MEMORY_LIMIT = "8G"
    TIME_LIMIT = 5 * 60
    SUITE = build_suite(
         os.environ.get("DOWNWARD_BENCHMARKS"),
        ["depot:p01.pddl", "grid:prob01.pddl", "gripper:prob01.pddl"]
    )


DRIVER_OPTIONS = [
    "--overall-time-limit",
    f"{TIME_LIMIT}s",
    "--overall-memory-limit",
    MEMORY_LIMIT,
    ]
CONFIGS = [
    (f"{index:02d}-{h_nick}", ["--search", f"astar(blind(), state_registry={h})"])
    for index, (h_nick, h) in enumerate(
        [
            ("pck", "packed"),
            ("fixed_packed", "fixed_tree_packed"),
            ("huffman_tree", "huffman"),
        ],
        start=1,
    )
]
REV_NICKS = [("valla", ""), ]
ATTRIBUTES = [
    "coverage",
    "error",
    "initial_h_value",
    "last_runlog_line",
    "memory",
    "plan_length",
    "planner_exit_code",
    "planner_time",
    "planner_wall_clock_time",
    "run_dir",
    "total_time",
    "translator_memory",
    "translator_time_done",
    "num_slots",
    "num_variables",
    "registered_states",
    "avg_num_var",
]

exp = Experiment(environment=ENV)
for rev, rev_nick in REV_NICKS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, project.get_repo_base(), rev, BUILD_OPTIONS)
    cached_rev.cache()
    exp.add_resource("", cached_rev.path, cached_rev.get_relative_exp_path())
    for config_nick, config in CONFIGS:
        print(rev_nick)
        algo_name = f"{rev_nick}-{config_nick}" if rev_nick else config_nick

        bounds = {}
        for task in SUITE:
            algo = FastDownwardAlgorithm(
                algo_name,
                cached_rev,
                DRIVER_OPTIONS,
                config,
            )
            run = FastDownwardRun(exp, algo, task)
            exp.add_run(run)

exp.add_parser(FastDownwardExperiment.EXITCODE_PARSER)
exp.add_parser(FastDownwardExperiment.TRANSLATOR_PARSER)
exp.add_parser(FastDownwardExperiment.SINGLE_SEARCH_PARSER)
exp.add_parser(FastDownwardExperiment.PLANNER_PARSER)
exp.add_parser(custom_parser.get_parser())

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_step("parse", exp.parse)
exp.add_fetcher(name="fetch")

project.add_report(
    exp,
    attributes=ATTRIBUTES,
)

# Parse the commandline and run the given steps.
exp.run_steps()