#! /usr/bin/env python

from downward.experiment import FastDownwardExperiment
from downward.suites import build_suite
from lab.environments import TetralithEnvironment, LocalEnvironment
from lab.reports import Attribute, geometric_mean, arithmetic_mean
from downward.reports.absolute import AbsoluteReport
from downward.cached_revision import CachedFastDownwardRevision
from downward.reports.compare import ComparativeReport
from downward.reports.scatter import ScatterPlotReport
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment
from downward import suites
import project
from project import report_names
import shutil
from pathlib import Path
from functools import partial
import json
from custom_parser import CommonParser

USER = project.dfsplan

BUILD = [] #debug
GENERATION_TIME = 100
REVISION_CACHE = project.DIR / "data" / "revision-cache"
REPO = project.get_repo_base()

if project.REMOTE:
    ENV = TetralithEnvironment(
        email="windy.phung@liu.se",
        extra_options="#SBATCH -A naiss2024-5-421",
        memory_per_cpu="9G",
    )
    HOURS = 0
    MIN = 30
    TIME_LIMIT = int(HOURS * 60 + MIN)
    MEMORY_LIMIT = "8G"
    SUITE = build_suite(project.DOMAINS_DIR, project.SUITE_OPTIMAL_STRIPS)
    BUILD += ["-j4"]
else:
    ENV = LocalEnvironment(processes=5)
    HOURS = 0
    MIN = 1
    TIME_LIMIT = int(HOURS * 60 + MIN)
    MEMORY_LIMIT = "3G"
    SUITE = project.SUITE_OPTIMAL_STRIPS_DEBUG_FREECELL
    # SUITE = project.SUITE_OPTIMAL_STRIPS_DEBUG
    # SUITE = project.SUITE_OPTIMAL_STRIPS_DEBUG_EXTENDED 
    GENERATION_TIME = 10
    BUILD += ["-j8"] # core angabe

DRIVER = [
    "--overall-time-limit",
    f"{TIME_LIMIT}m",
    "--overall-memory-limit",
    MEMORY_LIMIT
    # "--debug"
]


def no_search(run):
    if "search_start_time" not in run:
        error = run.get("error")
        if error is not None and error != "incomplete-search-found-no-plan":
            run["error"] = "no-search-due-to-" + error
    return run


def add_search_started(run):
    run["search_started"] = run.get("search_start_time") is not None
    return run


GIT_REV_WLR = "ebc5c9517313a29fba86b44b63e3d700053b1055"
GIT_REV_WOLR = "bbb134d94c4c59c2a09e4077b4e31c0006bf5d71"
exp = FastDownwardExperiment(environment=ENV)
exp.add_parser(FastDownwardExperiment.EXITCODE_PARSER)
exp.add_parser(FastDownwardExperiment.TRANSLATOR_PARSER)
exp.add_parser(FastDownwardExperiment.SINGLE_SEARCH_PARSER)
exp.add_parser(CommonParser())

exp.add_resource("", "project.py")

# abstractions = (
#     "[cartesian(subtasks=[goals(order=random,random_seed=5555)],random_seed=5555)]"
# ) random seed important if random orders


exp.add_algorithm(
    f"without label reduction",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]), 
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=1)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=1,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=2)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=2,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=3)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=3,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=4)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=4,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=5)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=5,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=10)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=10,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=20)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=20,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=50)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=50,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=100)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=100,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=500)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=500,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)
exp.add_algorithm(
    f"with label reduction (min_ops_per_label=1000)",
    project.SCORPION_DIR,
    GIT_REV_WLR,
    [
        "--search",
        f"astar(scp([cartesian(min_ops_per_label=1000,subtasks=[landmarks(order=random,random_seed=0)],random_seed=0),
        cartesian(subtasks=[goals(order=random,random_seed=0)]),
        projections(systematic(2), create_complete_transition_system=true)],
        max_orders=1K, diversify=false, max_time=infinity, max_optimization_time=0))",
    ],
    build_options=BUILD,
    driver_options=DRIVER,
)

exp.add_suite(project.DOMAINS_DIR, SUITE)

# Add step that writes experiment files to disk.
exp.add_step("build", exp.build)

# Add step that executes all runs.
exp.add_step("start", exp.start_runs)

exp.add_step("parse", exp.parse)
# Add step that collects properties from run directories and
# writes them to *-eval/properties.
exp.add_fetcher(name="fetch")

ATTRIBUTES = [ #schaue mal durch
    "error",
    "run_dir",
    "total_time",
    "coverage",
    "memory",
    "evaluations",
    "expansions",
    "expansions_until_last_jump",
    # "h_values",
    # "search_start_memory",
    "search_time",
    "num_transitions",
    "num_single_transitions",
    "num_labels",
    "num_reused_labels",
    "change_in_size",
    "cp_time",
    "search_start_time",
    "search_start_memory"
]

project.add_report(
    exp,
    attributes=ATTRIBUTES,
    filter=[
    ],
)

if project.REMOTE:
    project.add_compress_exp_dir_step(exp)
else:
    project.add_scp_step(exp, USER.scp_login, USER.remote_repo)
    project.add_report(
        exp,
        attributes=ATTRIBUTES,
        name=f"{exp.name}-{report_names[AbsoluteReport]}-cluster",
        eval_dir=project.get_cluster_eval_dir(exp),
        filter=[
        ],
    )


# def rename_algorithm(renames, run):
#     name = run["algorithm"]
#     if name in renames:
#         run["algorithm"] = renames[name]
#     return run


# def domain_as_category(run1, run2):
#     # run2['domain'] has the same value, because we always
#     # compare two runs of the same problem.
#     return run1["domain"]


# Parse the commandline and run the given steps.
exp.run_steps()
