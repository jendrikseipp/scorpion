#! /usr/bin/env python3

"""Solve some tasks with A* and the LM-Cut heuristic."""

import os
import os.path
import platform

from lab.environments import SlurmEnvironment

from downward.experiment import FastDownwardExperiment
from downward.reports.absolute import AbsoluteReport
from downward.reports.scatter import ScatterPlotReport

class FreiburgSlurmEnvironment(SlurmEnvironment):
    """Environment for Freiburg's AI group."""

    def __init__(self, **kwargs):
        SlurmEnvironment.__init__(self, **kwargs)

    DEFAULT_PARTITION =  "gki_cpu-cascadelake" # gki_gpu-ti
    DEFAULT_QOS = "normal"
    DEFAULT_MEMORY_PER_CPU = "4096M"


# Experiment Setup
SUITE = ['agricola-opt18-strips', 'airport', 'barman-opt11-strips',
    'barman-opt14-strips', 'blocks', 'childsnack-opt14-strips',
    'data-network-opt18-strips', 'depot', 'driverlog',
    'elevators-opt08-strips', 'elevators-opt11-strips',
    'floortile-opt11-strips', 'floortile-opt14-strips', 'freecell',
    'ged-opt14-strips', 'grid', 'gripper', 'hiking-opt14-strips',
    'logistics00', 'logistics98', 'miconic', 'movie', 'mprime',
    'mystery', 'nomystery-opt11-strips', 'openstacks-opt08-strips',
    'openstacks-opt11-strips', 'openstacks-opt14-strips',
    'openstacks-strips', 'organic-synthesis-opt18-strips',
    'organic-synthesis-split-opt18-strips', 'parcprinter-08-strips',
    'parcprinter-opt11-strips', 'parking-opt11-strips',
    'parking-opt14-strips', 'pathways-noneg', 'pegsol-08-strips',
    'pegsol-opt11-strips', 'petri-net-alignment-opt18-strips',
    'pipesworld-notankage', 'pipesworld-tankage', 'psr-small', 'rovers',
    'satellite', 'scanalyzer-08-strips', 'scanalyzer-opt11-strips',
    'snake-opt18-strips', 'sokoban-opt08-strips',
    'sokoban-opt11-strips', 'spider-opt18-strips', 'storage',
    'termes-opt18-strips', 'tetris-opt14-strips',
    'tidybot-opt11-strips', 'tidybot-opt14-strips', 'tpp',
    'transport-opt08-strips', 'transport-opt11-strips',
    'transport-opt14-strips', 'trucks-strips', 'visitall-opt11-strips',
    'visitall-opt14-strips', 'woodworking-opt08-strips',
    'woodworking-opt11-strips', 'zenotravel']

OVERALL_TIME_LIMIT = "30m"
OVERALL_MEMORY_LIMIT = "4000M"


# Use path to your Fast Downward repository.
REPO = "/home/speckd/git/scorpion/"
BRANCH = "wildcard_plans"
BENCHMARKS_DIR = "/home/speckd/benchmarks/downward-benchmarks/"
REVISION_CACHE = os.path.expanduser('~/lab/revision-cache')

ENV = FreiburgSlurmEnvironment()
exp = FastDownwardExperiment(environment=ENV, revision_cache=REVISION_CACHE)

# Add built-in parsers to the experiment.
exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)
exp.add_parser("parser.py")

exp.add_suite(BENCHMARKS_DIR, SUITE)

exp.add_algorithm('backtrack_optimistic', REPO, BRANCH, ['--search', 'astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=infinity, pick=MAX_REFINED, use_general_costs=true, debug=false, transform=no_transform(), cache_estimates=true, random_seed=-1,flaw_strategy=backtrack_optimistic))'], driver_options=["--overall-time-limit", OVERALL_TIME_LIMIT, "--overall-memory-limit", OVERALL_MEMORY_LIMIT])

exp.add_algorithm('backtrack_pessimistic', REPO, BRANCH, ['--search', 'astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=infinity, pick=MAX_REFINED, use_general_costs=true, debug=false, transform=no_transform(), cache_estimates=true, random_seed=-1,flaw_strategy=backtrack_pessimistic))'], driver_options=["--overall-time-limit", OVERALL_TIME_LIMIT, "--overall-memory-limit", OVERALL_MEMORY_LIMIT])

exp.add_algorithm('original', REPO, BRANCH, ['--search', 'astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=infinity, pick=MAX_REFINED, use_general_costs=true, debug=false, transform=no_transform(), cache_estimates=true, random_seed=-1,flaw_strategy=original))'], driver_options=["--overall-time-limit", OVERALL_TIME_LIMIT, "--overall-memory-limit", OVERALL_MEMORY_LIMIT])

exp.add_algorithm('optimistic', REPO, BRANCH, ['--search', 'astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=infinity, pick=MAX_REFINED, use_general_costs=true, debug=false, transform=no_transform(), cache_estimates=true, random_seed=-1,flaw_strategy=optimistic))'], driver_options=["--overall-time-limit", OVERALL_TIME_LIMIT, "--overall-memory-limit", OVERALL_MEMORY_LIMIT])

exp.add_algorithm('pessimistic', REPO, BRANCH, ['--search', 'astar(cegar(subtasks=[original()], max_states=infinity, max_transitions=infinity, max_time=infinity, pick=MAX_REFINED, use_general_costs=true, debug=false, transform=no_transform(), cache_estimates=true, random_seed=-1,flaw_strategy=pessimistic))'], driver_options=["--overall-time-limit", OVERALL_TIME_LIMIT, "--overall-memory-limit", OVERALL_MEMORY_LIMIT])

# Add step that writes experiment files to disk.
exp.add_step('build', exp.build)

# Add step that executes all runs.
exp.add_step('start', exp.start_runs)

# Add step that collects properties from run directories and
# writes them to *-eval/properties.
exp.add_fetcher(name='fetch')

# Add report step (AbsoluteReport is the standard report).
ATTRIBUTES = AbsoluteReport.PREDEFINED_ATTRIBUTES + AbsoluteReport.INFO_ATTRIBUTES + AbsoluteReport.ERROR_ATTRIBUTES + ["conrete_solution", "cartesian_states_to_solve", "cartesian_states", "run_dir"]
exp.add_report(AbsoluteReport(attributes=ATTRIBUTES), outfile='report.html')

# Parse the commandline and show or run experiment steps.
exp.run_steps()
