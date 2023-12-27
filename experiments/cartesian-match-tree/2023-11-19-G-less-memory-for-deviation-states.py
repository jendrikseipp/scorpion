#! /usr/bin/env python

import os
import shutil

import custom_parser

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment

import project


REPO = project.get_repo_base()
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
SUITE = ["depot:p01.pddl", "grid:prob01.pddl", "gripper:prob01.pddl", "parcprinter-08-strips:p01.pddl", "mystery:prob07.pddl"]
REVISION_CACHE = (
    os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
if project.REMOTE:
    ENV = project.TetralithEnvironment(email="jendrik.seipp@liu.se", memory_per_cpu="5G", extra_options="#SBATCH --account=naiss2023-5-314")
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    (f"{id:02d}-{transition_representation}{'-cache' if store_spt_children and store_spt_parents else ''}", ["--search", f"astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time={max_time}, sort_transitions=true, transition_representation={transition_representation}, pick_flawed_abstract_state={pick_state}, pick_split={split}, tiebreak_split={tiebreak_split}, memory_padding=512, random_seed=0, max_concrete_states_per_abstract_state=1K, max_state_expansions=1M, store_shortest_path_tree_children={store_spt_children}, store_shortest_path_tree_parents={store_spt_parents}, use_max=true), verbosity=normal)"])
    for id, transition_representation in enumerate([
        #"store",
        #"naive",
        #"sg",
        #"rh",
        "sg_rh",
    ], start=1)
    for _, pick_state, split, tiebreak_split in [
        #("single", "first_on_shortest_path", "max_refined", "min_cg"),
        ("batch", "batch_min_h", "max_cover", "max_refined"),
    ]
    for store_spt_children, store_spt_parents in ([
        (False, False),
        #(True, False),
        (True, True),
    ] if transition_representation in {"sg_rh"} else [
        (False, False),
    ])
    for max_time in [1200]
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--validate",
    "--overall-time-limit", "30m",
    "--overall-memory-limit", "4G",
]
# Pairs of revision identifier and revision nick.
REV_NICKS = [
    ("4c8c19854", "13-loop-over-pairs"),
    ("727b961b2", "14-pack-deviation-states"),
    ("7f458d505", "15-use-fact-map"),
    ("774f5d8da", "16-directly-fill-fact-map"),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "total_time",
    "cost",
    "coverage",
    "expansions_until_last_jump",
    "memory",
    "initial_h_value",
    "search_start_time_score", "search_start_memory_score",
    "time_for_searching_abstract_traces",
    "time_for_finding_flaws_and_computing_splits",
    "time_for_splitting_states",
    "time_for_updating_goal_distances",
    "time_for_building_abstraction",
    project.Attribute("cegar_found_concrete_solution", min_wins=False, absolute=True),
    project.Attribute("cegar_proved_unsolvability", min_wins=False, absolute=True),
    "cegar_*", "cartesian_*", "successor_generator_*",
]

exp = Experiment(environment=ENV)
for rev, rev_nick in REV_NICKS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, REPO, rev, BUILD_OPTIONS)
    cached_rev.cache()
    exp.add_resource("", cached_rev.path, cached_rev.get_relative_exp_path())
    for config_nick, config in CONFIGS:
        algo_name = f"{config_nick}:{rev_nick}" if rev_nick else config_nick

        for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
            # Silence parcprinter runs to avoid huge logfiles.
            task_config = config.copy()
            if "parcprinter" in task.domain:
                task_config[-1] = task_config[-1].replace("verbosity=normal", "verbosity=silent")
            algo = FastDownwardAlgorithm(
                algo_name,
                cached_rev,
                DRIVER_OPTIONS,
                task_config,
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

if not project.REMOTE:
    exp.add_step("remove-eval-dir", shutil.rmtree, exp.eval_dir, ignore_errors=True)
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)

filter = project.OptimalityCheckFilter()

project.add_absolute_report(
    exp,
    attributes=ATTRIBUTES,
    filter=[project.check_initial_h_value, filter.check_costs, project.check_search_started, project.add_evaluations_per_time, project.group_domains],
)

def cegar_found_no_solution(run):
    if run.get("cegar_found_concrete_solution") == 1:
        run["cartesian_states"] = None
    return run

algos = [f"{config_nick}:{rev_nick}" if rev_nick else f"{config_nick}" for _, rev_nick in REV_NICKS for config_nick, _ in CONFIGS]
algo_pairs = list(zip(algos, algos[1:]))
project.add_scatter_plot_reports(exp, algo_pairs, attributes=[project.Attribute("cartesian_states", min_wins=False)], filter=cegar_found_no_solution)

exp.run_steps()
