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

MAX_TIME_OUTER = 100 if project.REMOTE else 0.1
MAX_TIME_INNER = 10 if project.REMOTE else 0.01
CONFIGS = [
    (f"scp", ["--search", f"""astar(scp_online([
        projections(sys_scp(max_time={MAX_TIME_OUTER}, max_time_per_restart={MAX_TIME_INNER})),
        cartesian()],
        saturator=perimstar, max_time=100, interval=1K, orders=greedy_orders()),
        pruning=limited_pruning(pruning=atom_centric_stubborn_sets(), min_required_pruning_ratio=0.2))"""])
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--validate",
    "--overall-time-limit", "30m",
    "--overall-memory-limit", "4G",
    "--transform-task", "preprocess-h2",
]

# Pairs of revision identifier and revision nick.
REV_NICKS = [
    ("ab740d97a", "00-convert-ancestors"),
    ("b0f8cc484", "01-unseq-get-abs-ids"),
    ("9b0a9c0f2", "02-no-tmp-state-in-ref-hierarchy"),
    ("e8dbf5557", "03-transform-reduce-compute-h"),
    ("611e78f88", "04-unseq-compute-h"),
    ("9c32c175b", "05-print-useful-abstractions"),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "search_time",
    "score_search_time",
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
    "useful_abstractions_ratio",
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

filters = [project.check_initial_h_value, filter.check_costs, project.check_search_started, project.add_evaluations_per_time, project.group_domains]

project.add_absolute_report(
    exp,
    attributes=ATTRIBUTES,
    filter=filters,
)

def get_algos():
    for rev, rev_nick in REV_NICKS:
        for config_nick, config in CONFIGS:
            algo_name = f"{rev_nick}:{config_nick}" if rev_nick else config_nick
            yield algo_name

algos = list(get_algos())
algo_pairs = list(zip(algos, algos[1:]))
project.add_scatter_plot_reports(exp, algo_pairs, attributes=["search_time"], filter=[])

exp.run_steps()
