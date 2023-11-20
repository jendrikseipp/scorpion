#! /usr/bin/env python

from pathlib import Path

from lab.experiment import Experiment

from labreports.cactus_plot import CactusPlot

import project


ATTRIBUTES = [
    "error",
    "run_dir",
    "search_start_time",
    "search_start_memory",
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

exp = Experiment()
exp.add_step(
    "remove-combined-properties", project.remove_file, Path(exp.eval_dir) / "properties"
)

project.fetch_algorithm(exp, "2023-11-17-B-abstract-flaw-search-clear-f-optimal-states", "batch-ts-children=False-parents=False-abstract=False", new_algo="ts")
project.fetch_algorithm(exp, "2023-11-17-C-abstract-flaw-search-max-conc-states", "batch-sg-children=False-parents=False-abstract=False-max-conc=1K", new_algo="sg")
project.fetch_algorithm(exp, "2023-11-17-B-abstract-flaw-search-clear-f-optimal-states", "batch-sg-children=True-parents=True-abstract=False", new_algo="sg-cache")

filters = [project.add_evaluations_per_time]

project.add_absolute_report(
    exp, attributes=ATTRIBUTES, filter=filters, filter_cegar_found_concrete_solution=1, name=f"{exp.name}"
)

def combine_cegar_outcomes(run):
    if run.get("cegar_reached_memory_limit_in_flaw_search"):
        run["cegar_reached_memory_limit"] = 1
    del run["cegar_reached_memory_limit_in_flaw_search"]
    if run.get("cegar_reached_time_limit_in_flaw_search"):
        run["cegar_reached_time_limit"] = 1
    del run["cegar_reached_time_limit_in_flaw_search"]
    return run

for attribute in ["cegar_reached_time_limit", "cegar_reached_memory_limit", "cegar_found_concrete_solution"]:
    exp.add_report(CactusPlot(attributes=[attribute, "time_for_building_abstraction"], filter=[combine_cegar_outcomes]), outfile=f"cactus-plot-{attribute}.tex")

def cegar_found_solution(run):
    if run.get("cegar_found_concrete_solution") != 1:
        run["time_for_building_abstraction"] = None
    return run

def limit_h_value(run):
    if run.get("initial_h_value", 1000  ) > 100:
        run["initial_h_value"] = None
    return run

project.add_scatter_plot_reports(exp, [("ts", "sg-cache")], attributes=["initial_h_value"], filter=[cegar_found_solution, limit_h_value])

exp.run_steps()
