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
from lab import environments, tools
import project
from itertools import product

def get_default_data_dir():
    """E.g. "ham/spam/eggs.py" => "ham/spam/data/"."""
    return os.path.join(os.path.dirname(tools.get_script_path()), "data")


def _get_default_experiment_name():
    """Get default name for experiment.

    Derived from the filename of the main script, e.g.
    "ham/spam/eggs.py" => "eggs".
    """
    return os.path.splitext(os.path.basename(tools.get_script_path()))[0]


REVISION_CACHE = (
        os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
BUILD_OPTIONS = []
if project.REMOTE:
    ENV = TetralithEnvironment(
        setup=TetralithEnvironment.DEFAULT_SETUP,
        email="olijo92@liu.se",
        extra_options="#SBATCH -A naiss2025-5-382",
        memory_per_cpu="9G",
    )
    TIME_LIMIT = 5 * 60 * 60 #5 hours
    MEMORY_LIMIT = "8G"

    base_path = Path(os.environ.get("DOWNWARD_BENCHMARKS")) / "pddl-benchmarks"

    SUITE_SPECS = [
       # ("autoscale-benchmarks-main/21.11-optimal-strips", SUITE_AUTOSCALE_OPTIMAL_STRIPS),
        ("beluga2025", SUITE_BELUGA2025_SCALABILITY_DETERMINISTIC),
        ("pushworld", SUITE_PUSHWORLD),
        ("mine-pddl", SUITE_MINEPDDL),
        ("htg-domains", SUITE_HTG),
    ]

    BASE_SUITES = [
        ("ipc2024-optimal-strips", SUITE_IPC_OPTIMAL_STRIPS),
        ("ipc2024-optimal-adl", SUITE_IPC_OPTIMAL_ADL),
        ("ipc2023-learning-track", SUITE_IPC_LEARNING)
    ]

    SUITE = []
    for rel_path, suite in SUITE_SPECS:
        SUITE += list(build_suite(base_path / rel_path, suite))
    for rel_path, suite in BASE_SUITES:
        SUITE += list(build_suite(os.environ.get("DOWNWARD_BENCHMARKS"), suite))
else:
    ENV = LocalEnvironment(processes=1)
    MEMORY_LIMIT = "6G"
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
state_registries = [
               ("unpck", "unpacked"),
               ("pck", "packed"),
               ("static_unpck", "tree_unpacked"),
               ("static_pck", "tree_packed"),
               # ("fixed_unpck", "fixed_tree_unpacked"),
               # ("fixed_pck", "fixed_tree_packed"),
               # ("huffman_tree", "huffman"),

            ]

heuristics = [
                ("blind", "blind(cache_estimates=false)"),
                ("scp", "scp([projections(systematic(2))], saturator=perimstar, max_time=10, diversify=true, max_optimization_time=0, orders=greedy_orders(), cache_estimates=false)")
            ]

CONFIGS = [
    (f"{index:02d}-{h_nick}-{s_nick}", ["--search", f"astar({h}, state_registry={s})"])
    for index, ((s_nick, s), (h_nick, h)) in enumerate(
        product(state_registries, heuristics),
        start=1,
    )
]
REV_NICKS = [("reduces_search_node", "")]
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
    "num_atoms",
    "registered_states",
    "avg_edges_per_state",
    "state_set_occupied_tree",
    "state_set_allocated_tree",
    "state_set_size",
    "score_planner_memory"
]
def build_exp(additional_options=[], nick=None): # this is for running seperate experiments with both pruning and no
    if nick is not None:
        exp_path = get_default_data_dir() + "/" + _get_default_experiment_name() + "-" + nick
        print(exp_path)
        if not os.path.isdir(exp_path):
            os.makedirs(exp_path)
    else:
        exp_path = None

    exp = Experiment(environment=ENV, path=exp_path)

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
                    additional_options + config,
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
    
    return exp



exp = build_exp()
project.add_report(
    exp,
    attributes=ATTRIBUTES
)
exp.run_steps()
