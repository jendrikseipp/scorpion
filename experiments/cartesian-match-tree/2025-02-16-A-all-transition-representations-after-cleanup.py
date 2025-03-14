#! /usr/bin/env python

import os
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

MAX_TRANSITIONS = {
    "store": ["infinity"],
    "naive": [0],
    "sg": [0],
    "rh": [0],
    "sg_rh": [0, "1M", "10M", "100M", "infinity"],
}
CONFIGS = [
    (f"{id:02d}-{transition_representation}-{max_transitions}", ["--search", f"astar(cegar(subtasks=[original()], max_states=infinity, max_transitions={max_transitions}, max_time={max_time}, sort_transitions=true, transition_representation={transition_representation}, pick_flawed_abstract_state={pick_state}, pick_split={split}, tiebreak_split={tiebreak_split}, memory_padding=512, random_seed=0, max_concrete_states_per_abstract_state=1K, max_state_expansions=1M, verbosity=normal))"])
    for id, transition_representation in enumerate([
        "store",
        "naive",
        "sg",
        "rh",
        "sg_rh",
    ], start=1)
    for _, pick_state, split, tiebreak_split in [
        #("single", "first_on_shortest_path", "max_refined", "min_cg"),
        ("batch", "batch_min_h", "max_cover", "max_refined"),
    ]
    for max_transitions in MAX_TRANSITIONS[transition_representation]
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
    ("8bf6a2ab0", ""),
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
    "search_time",
    "score_search_time",
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
        algo_name = f"{rev_nick}:{config_nick}" if rev_nick else config_nick

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

filter = project.OptimalityCheckFilter()

filters = [project.check_initial_h_value, filter.check_costs, project.check_search_started, project.add_evaluations_per_time, project.group_domains]

project.add_absolute_report(
    exp,
    attributes=ATTRIBUTES,
    filter=filters,
)

if not project.REMOTE:
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)

def add_cegar_outcome(run):
    if run.get("error") == "exitcode-250":
        run["cegar_outcome"] = "cegar_reached_memory_limit"
    elif run.get("error") == "translate-out-of-memory":
        run["cegar_outcome"] = "translator-out-of-memory"
    elif run.get("error") == "search-out-of-time" and run.get("search_start_time") is None:
        run["cegar_outcome"] = "cegar_reached_time_limit"
    elif run.get("cegar_outcome") in ["cegar_proved_unsolvability", "cegar_found_concrete_solution"]:
        run["cegar_outcome"] = "solved-during-refinement"
    elif run.get("cegar_outcome") == "cegar_reached_memory_limit_in_flaw_search":
        run["cegar_outcome"] = "cegar_reached_memory_limit"
    elif run.get("cegar_outcome") == "cegar_reached_time_limit_in_flaw_search":
        run["cegar_outcome"] = "cegar_reached_time_limit"
    return run

def add_search_outcome(run):
    if run.get("cegar_outcome") == "solved-during-refinement":
        run["error"] = "solved-during-refinement"
    elif run.get("error") == "exitcode-250":
        run["error"] = "search-out-of-memory"
    elif run.get("error") == "success":
        run["error"] = "solved-during-search"
    return run

def add_overall_outcome(run):
    if run.get("cegar_outcome") == "solved-during-refinement" or run.get("error") == "solved-during-search":
        run["error"] = "solved-overall"
    return run

exp.add_report(PerAlgorithmCounterReport(attributes=["cegar_outcome"], filter=filters + [add_cegar_outcome], format="tex" if project.TEX else "html"), name="cegar-outcomes")
exp.add_report(PerAlgorithmCounterReport(attributes=["error"], filter=filters + [add_cegar_outcome, add_search_outcome], format="tex" if project.TEX else "html"), name="search-outcomes")
exp.add_report(PerAlgorithmCounterReport(attributes=["error"], filter=filters + [add_cegar_outcome, add_search_outcome, add_overall_outcome], format="tex" if project.TEX else "html"), name="overall-outcomes")

exp.add_report(PerAlgorithmCounterReport(attributes=["expansions_until_last_jump"], filter=filters, format="tex" if project.TEX else "html"), name="expansions-until-last-jump")

project.add_absolute_report(
    exp, attributes=ATTRIBUTES, filter=filters, filter_cegar_found_concrete_solution=1, name=f"{exp.name}-solved"
)

def cegar_found_no_solution(run):
    if run.get("cegar_found_concrete_solution") == 1:
        run["cartesian_states"] = None
    return run

def cegar_found_solution(run):
    if not run.get("cegar_found_concrete_solution"):
        run["time_for_building_abstraction"] = None
        run["search_start_memory"] = None
    return run

project.add_scatter_plot_reports(exp, [
    ("02-naive", "03-sg"),
    ], attributes=["time_for_building_abstraction"], filter=cegar_found_solution)

def subtract_memory_padding(run):
    if run.get("search_start_memory") is not None:
        run["search_start_memory"] -= 512 * 1024
    return run

def limit_init_h(run):
    if "initial_h_value" in run and run["initial_h_value"] > 100:
        run["initial_h_value"] = None
    return run

project.add_scatter_plot_reports(exp, [
    ("01-store", "05-sg_rh"),
    ("01-store", "05-sg_rh-cache"),
    ], attributes=["time_for_building_abstraction", "expansions_until_last_jump", "initial_h_value", "search_start_memory"], filter=[cegar_found_solution, subtract_memory_padding, limit_init_h])

exp.run_steps()
