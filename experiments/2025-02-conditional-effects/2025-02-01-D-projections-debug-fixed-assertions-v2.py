#! /usr/bin/env python3

from pathlib import Path

from downward.experiment import FastDownwardExperiment

import custom_parser
import project

SUITE = [
    "briefcaseworld",
    #"burnt-pancakes-factored",
    "caldera",
    "caldera-split",
    "cavediving-14-adl",
    "citycar-opt14-adl",
    "cnot-synthesis-lifted",
    "cnot-synthesis-lifted-hard",
    "cnot-synthesis-lifted-map",
    "fsc-blocks",
    "fsc-grid-a1",
    "fsc-grid-a2",
    "fsc-grid-r",
    "fsc-hall",
    "fsc-visualmarker",
    "ged1-ds1",
    "gedp-ds2ndp",
    #"matrix-multiplication",
    "miconic-simpleadl",
    "nurikabe",
    #"pancakes-factored",
    "rubiks-cube",
    #"rubiks-cube-factored",
    "settlers",
    "spider",
    "t0-adder",
    "t0-coins",
    "t0-comm",
    "t0-grid-dispose",
    "t0-grid-lookandgrab",
    "t0-grid-push",
    "t0-grid-trash",
    "t0-sortnet",
    "t0-sortnet-alt",
    "t0-uts",
    #"topspin-factored",
]


ENV = project.TetralithEnvironment(email="jendrik.seipp@liu.se", memory_per_cpu="8G", extra_options="#SBATCH --account=naiss2024-5-421")
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
REPO = "/proj/dfsplan/users/x_jense/scorpion-dev"
DIR = Path(__file__).parent
BENCHMARKS_DIR = "/proj/dfsplan/users/x_jense/conditional-effects-for-abstractions/experiments/benchmarks/pddl"
# If REVISION_CACHE is None, the default ./data/revision-cache is used.
REVISION_CACHE = None
REV_NICKS = [
    ("9b433b0d6", "04-fixed-assertions-v2"),
]

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
    BENCHMARKS_DIR = Path.home() / "Uni" / "papers" / "conditional-effects-for-abstractions" / "experiments" / "benchmarks" / "pddl"
    SUITE = ["briefcaseworld:p01.pddl"]

exp = FastDownwardExperiment(environment=ENV, revision_cache=REVISION_CACHE)

# Add built-in parsers to the experiment.
exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)
exp.add_parser(custom_parser.get_parser())

exp.add_suite(BENCHMARKS_DIR, SUITE)
sas_driver_options = [
    "--overall-time-limit",
    "1800",
    "--overall-memory-limit",
    "8000M",
    "--transform-task",
    "preprocess-h2",
]

projections_opts = "projections(sys_scp(max_time=900, max_time_per_restart=90, max_pdb_size=2M, max_collection_size=20M, pattern_type=interesting_non_negative, create_complete_transition_system=true), create_complete_transition_system=true)"
other_scp_online_opts = (
    "saturator=perimstar, max_time=100, interval=10K, orders=greedy_orders()"
)

configs = [
    ("projections", ["--search", f"astar(scp_online([{projections_opts}], {other_scp_online_opts}))"]),
]

for rev, rev_nick in REV_NICKS:
    for name, config in configs:
        exp.add_algorithm(
            f"{rev_nick}-{name}",
            REPO,
            rev,
            config,
            build_options=["debug", "-j4"],
            driver_options=sas_driver_options + ["--build", "debug"],
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
