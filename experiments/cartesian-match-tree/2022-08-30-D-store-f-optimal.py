#! /usr/bin/env python

import os
import os.path
import shutil

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import (
    _DownwardAlgorithm,
    _get_solver_resource_name,
    FastDownwardRun,
)
from lab.experiment import Experiment, get_default_data_dir

import project


REPO = project.get_repo_base()
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
SUITE = ["depot:p01.pddl", "grid:prob01.pddl", "gripper:prob01.pddl"]
REVISION_CACHE = os.environ.get("DOWNWARD_REVISION_CACHE") or os.path.join(get_default_data_dir(), "revision-cache")
if project.REMOTE:
    ENV = project.TetralithEnvironment(email="jendrik.seipp@liu.se", memory_per_cpu="4400M", extra_options="#SBATCH --account=snic2022-5-341")
    SUITE = project.SUITE_OPTIMAL_STRIPS
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    (f"{nick}-{transition_representation}-children={store_spt_children}-parents={store_spt_parents}", ["--search", f"astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=infinity, sort_transitions=true, transition_representation={transition_representation}, pick_flawed_abstract_state={pick_state}, pick_split={split}, tiebreak_split={tiebreak_split}, memory_padding=0, random_seed=0, max_concrete_states_per_abstract_state=infinity, max_state_expansions=1M, store_shortest_path_tree_children={store_spt_children}, store_shortest_path_tree_parents={store_spt_parents}), bound=0)"])
    for transition_representation in [
        "ts",
        "sg",
    ]
    for nick, pick_state, split, tiebreak_split in [
        ("single", "first_on_shortest_path", "max_refined", "min_cg"),
        ("batch", "batch_min_h", "max_cover", "max_refined"),
    ]
    for store_spt_children, store_spt_parents in [(False, False), (True, False), (True, True)]
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--validate",
    "--overall-time-limit", "5m",
    "--overall-memory-limit", "4G",
]
# Pairs of revision identifier and revision nick.
REVS = [
    ("85fa496eb", ""),
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
for rev, rev_nick in REVS:
    cached_rev = CachedFastDownwardRevision(REPO, rev, BUILD_OPTIONS)
    cached_rev.cache(REVISION_CACHE)
    cache_path = os.path.join(REVISION_CACHE, cached_rev.name)
    dest_path = "code-" + cached_rev.name
    exp.add_resource("", cache_path, dest_path)
    # Overwrite the script to set an environment variable.
    exp.add_resource(
        _get_solver_resource_name(cached_rev),
        os.path.join(cache_path, "fast-downward.py"),
        os.path.join(dest_path, "fast-downward.py"),
    )
    for config_nick, config in CONFIGS:
        algo_name = f"{rev_nick}-{config_nick}" if rev_nick else config_nick

        for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
            algo = _DownwardAlgorithm(
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
exp.add_parser(project.DIR / "parser.py")
exp.add_parser(project.FastDownwardExperiment.PLANNER_PARSER)

exp.add_step("build", exp.build)
exp.add_step("start", exp.start_runs)
exp.add_fetcher(name="fetch")

if not project.REMOTE:
    exp.add_step("remove-eval-dir", shutil.rmtree, exp.eval_dir, ignore_errors=True)
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)

project.add_absolute_report(
    exp, attributes=ATTRIBUTES, filter=[project.add_evaluations_per_time, project.group_domains],
)

exp.run_steps()
