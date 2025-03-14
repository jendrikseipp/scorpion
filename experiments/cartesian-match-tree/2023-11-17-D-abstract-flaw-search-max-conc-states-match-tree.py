#! /usr/bin/env python

import os
import os.path
import shutil

import custom_parser

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment

from labreports.counter import PerAlgorithmCounterReport

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
    ENV = project.TetralithEnvironment(email="jendrik.seipp@liu.se", memory_per_cpu="5G", extra_options="#SBATCH --account=naiss2024-5-421")
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    (f"{nick}-{transition_representation}-children={store_spt_children}-parents={store_spt_parents}-max-conc={max_concrete_states_per_abstract_state}-abstract={use_abstract_flaw_search}", ["--search", f"astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=1800, sort_transitions=true, transition_representation={transition_representation}, pick_flawed_abstract_state={pick_state}, pick_split={split}, tiebreak_split={tiebreak_split}, memory_padding=512, random_seed=0, max_concrete_states_per_abstract_state={max_concrete_states_per_abstract_state}, max_state_expansions=1M, store_shortest_path_tree_children={store_spt_children}, store_shortest_path_tree_parents={store_spt_parents}, use_abstract_flaw_search={use_abstract_flaw_search}), bound=0)"])
    for transition_representation in [
        #"ts",
        "sg",
    ]
    for nick, pick_state, split, tiebreak_split in [
        #("single", "first_on_shortest_path", "max_refined", "min_cg"),
        ("batch", "batch_min_h", "max_cover", "max_refined"),
    ]
    for store_spt_children, store_spt_parents in ([
        #(False, False),
        #(True, False),
        (True, True),
    ] if transition_representation == "sg" else [
        (False, False),
    ])
    for use_abstract_flaw_search in [True, False]
    for max_concrete_states_per_abstract_state in ["10", "100", "1K", "10K", "100K", "infinity"]
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--validate",
    "--overall-time-limit", "60m",
    "--overall-memory-limit", "4608M",  #  4 GiB + 512 MiB
]
# Pairs of revision identifier and revision nick.
REV_NICKS = [
    ("17aed0722", ""),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    #"total_time",
    #"coverage",
    #"expansions_until_last_jump",
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
        algo_name = f"{rev_nick}-{config_nick}" if rev_nick else config_nick

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

project.add_absolute_report(
    exp, attributes=ATTRIBUTES, filter=[project.add_evaluations_per_time, project.group_domains],
)

if not project.REMOTE:
    #exp.add_step("remove-eval-dir", shutil.rmtree, exp.eval_dir, ignore_errors=True)
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)

def cegar_found_solution(run):
    if run.get("cegar_found_concrete_solution") != 1:
        run["time_for_building_abstraction"] = None
    return run

project.add_scatter_plot_reports(exp, [("batch-sg-children=False-parents=False-abstract=False-max-conc=1K", "batch-sg-children=False-parents=False-abstract=True-max-conc=1K")], attributes=["time_for_building_abstraction", "search_start_memory"], filter=cegar_found_solution)

exp.run_steps()
