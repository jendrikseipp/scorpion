#! /usr/bin/env python

import os
from pathlib import Path

import custom_parser
import project

from downward import suites
from downward.cached_revision import CachedFastDownwardRevision
from downward.experiment import FastDownwardAlgorithm, FastDownwardRun
from lab.experiment import Experiment

REPO = project.get_repo_base()
BENCHMARKS_DIR = Path(os.environ["AUTOSCALE_BENCHMARKS_SAT"])
assert BENCHMARKS_DIR.is_dir(), f"Benchmarks directory {BENCHMARKS_DIR} does not exist"
SCP_LOGIN = "nsc"
REMOTE_REPOS_DIR = "/proj/dfsplan/users/x_jense/"
SUITE = ["depots:p01.pddl", "grid:p01.pddl", "gripper:p01.pddl"]
REVISION_CACHE = (
    os.environ.get("DOWNWARD_REVISION_CACHE") or project.DIR / "data" / "revision-cache"
)
if project.REMOTE:
    # ENV = project.BaselSlurmEnvironment(email="my.name@myhost.ch")
    ENV = project.TetralithEnvironment(
        memory_per_cpu="9G",  # leave some space for the scripts
        email="jendrik.seipp@liu.se",
        extra_options="#SBATCH --account=naiss2025-5-382",
    )
    SUITE = project.SUITE_AUTOSCALE
else:
    ENV = project.LocalEnvironment(processes=2)

CONFIGS = [
    ("blind", ["--preprocess-options", "--h2-time-limit", "9999", "--search-options", "--search", "astar(blind(), bound=0)"]),
]
BUILD_OPTIONS = []
DRIVER_OPTIONS = [
    "--preprocess",
    "--validate",
    "--overall-time-limit",
    "30m",
    "--overall-memory-limit",
    "8G",
    "--keep-sas-file",
]

"""
○  rvosulqw jendrikseipp@gmail.com 2026-03-30 12:01:08 a95c6181
│  Fix in_add_or_del computation.
○  tqmrzkql jendrikseipp@gmail.com 2026-03-29 22:25:54 7c675fe5
│  Revise exp.
○  kxzvkpuy jendrikseipp@gmail.com 2026-03-29 22:25:54 preprocessor-autoresearch-digest* bc7d5876
│  Add exp.
○  vtnxkuks jendrik.seipp@liu.se 2026-03-29 22:25:54 908f0c3f
│  Made `remove_ambiguity` reuse candidate-vector storage across operators instead of destroying and reallocating the nested candidate lists each call. The scratch buffer now keeps inactive candidate slots alive and tracks an active `candidate_count`, so pruning rounds just swap among reusable slots. Checks passed and preprocess_total_cpu_s improved to 100.43s.
○  nnwpsosp jendrik.seipp@liu.se 2026-03-29 22:25:54 be87ff41
│  Replaced the packed bitsets used by `apply_operator` (`operator_atom_cache` and the per-pass add/delete membership table) with byte-addressable arrays. This spends more memory but removes bit-twiddling and extra indexing from the hottest h^2 propagation loops; checks still passed, and preprocess_total_cpu_s improved sharply to 112.46s.
○  powruwyp jendrik.seipp@liu.se 2026-03-29 21:22:36 703259d8
│  Removed `std::endl` flushes from the preprocessed-SAS writers (`generate_cpp_input` and related operator/axiom/variable/mutex serializers), switching file output to newline writes without per-line flushing. The benchmark includes writing the full preprocessed task, so this cut a large amount of avoidable I/O overhead. Checks passed and preprocess_total_cpu_s improved to 144.39s.
○  pxxwotwu jendrikseipp@gmail.com 2026-03-29 18:21:55 cc8ca658
│  Add preprocess speed test.
○  zxlyzunu jendrikseipp@gmail.com 2026-03-29 18:14:21 f5ed816a
│  Fix overflow in preprocessor.
○  mssktwxy jendrikseipp@gmail.com 2026-03-20 10:40:29 scorpion fccb1abd
   Fix style.
"""

# Pairs of revision identifier and optional revision nick.
REV_NICKS = [
    ("7d742a2d", "01-base"),
    ("fccb1abd", "02-fix-style"),
    ("f5ed816a", "03-fix-overflow"),
    ("cc8ca658", "50-add-preprocessor-speed-tests"),
    ("703259d8", "51-no-endl-sas-write"),
    ("9ff51e9c", "52-byte-apply-caches"),
    ("982bd41f", "53-reuse-candidate-slots"),
    ("d95fb4a3", "54-bitmasks"),
]
ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
    "total_time",
    "h_values",
    "coverage",
    "expansions",
    "memory",
    project.EVALUATIONS_PER_TIME,
    "preprocessor_*",
]

exp = Experiment(environment=ENV)
for rev, rev_nick in REV_NICKS:
    cached_rev = CachedFastDownwardRevision(REVISION_CACHE, REPO, rev, BUILD_OPTIONS)
    cached_rev.cache()
    exp.add_resource("", cached_rev.path, cached_rev.get_relative_exp_path())

    for config_nick, config in CONFIGS:
        myconfig = config.copy()
        if "base" in rev_nick:
            myconfig = [opt.replace("--h2-time-limit", "--h2_time_limit") for opt in myconfig]
        algo_name = f"{rev_nick}-{config_nick}" if rev_nick else config_nick

        for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
            algo = FastDownwardAlgorithm(
                algo_name,
                cached_rev,
                DRIVER_OPTIONS,
                myconfig,
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
    exp,
    attributes=ATTRIBUTES,
    filter=[project.add_evaluations_per_time, project.group_domains],
)
if not project.REMOTE:
    project.add_scp_step(exp, SCP_LOGIN, REMOTE_REPOS_DIR)
project.add_compress_exp_dir_step(exp)

project.add_scatter_plot_reports(exp, [
        ("01-base-blind", "50-add-preprocessor-speed-tests-blind"),
        ("50-add-preprocessor-speed-tests-blind", "51-no-endl-sas-write-blind"),
        ("51-no-endl-sas-write-blind", "52-byte-apply-caches-blind"),
        ("52-byte-apply-caches-blind", "53-reuse-candidate-slots-blind"),
        ("53-reuse-candidate-slots-blind", "54-fix-in-add-or-del-blind"),
    ], ["preprocessor_time"])

exp.run_steps()
